"""The trial-029 image contract. No colour grading, blending or model conversion."""
import functools
import hashlib
import struct
import time

import numpy as np

WIDTH, HEIGHT = 960, 540
PIXELS = WIDTH * HEIGHT * 4
HEADER = struct.pack('<2sIHHIIiiHHIIiiII', b'BM', PIXELS + 54, 0, 0, 54,
                     40, WIDTH, -HEIGHT, 1, 32, 0, PIXELS, 0, 0, 0, 0)
CHECKPOINT = '0fbb08561b71ad759c4503a77233497f06c7b168ba8eaf023a35b214d71af254'
REFERENCE = {
    'input-24.bmp': 'c3e2643858ca11f67569234e226398f9190f6d1e097a3e287f867095dcf91d73',
    'output-24.bmp': 'de77e40bf2c9d440453767dafe327250f27b3d0b255a266e861d84b7ec776b5a',
    'raw-024.npy': '0927237715576cffcd0932c2e216b752125a5f4ca2d733fac3b35e78ec98e996',
}

# This procedural fixture contains no game pixels. It is *not* the historical
# image-quality oracle: it only checks the service/slot round trip.
SYNTHETIC_REFERENCE = 'synthetic-bgra-v1'


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(4 * 1024**2), b''):
            h.update(block)
    return h.hexdigest()


def verify_references(directory):
    for name, digest in REFERENCE.items():
        if sha(directory / name) != digest:
            raise ValueError('reference hash differs: ' + name)


def bmp_to_rgb(data):
    if len(data) != 54 + PIXELS or data[:54] != HEADER:
        raise ValueError('unexpected or incomplete frame BMP')
    bgra = np.frombuffer(data, dtype=np.uint8, offset=54).reshape(HEIGHT, WIDTH, 4)
    return bgra[:, :, [2, 1, 0]].copy()


def bgra_to_rgb(data):
    if len(data) != PIXELS:
        raise ValueError('incomplete BGRA frame')
    return np.frombuffer(data, dtype=np.uint8).reshape(HEIGHT, WIDTH, 4)[:, :, [2, 1, 0]].copy()


def rgb_to_bgra(rgb):
    if rgb.shape != (HEIGHT, WIDTH, 3) or rgb.dtype != np.uint8:
        raise ValueError('bad output image')
    bgra = np.empty((HEIGHT, WIDTH, 4), dtype=np.uint8)
    bgra[:, :, :3] = rgb[:, :, ::-1]
    bgra[:, :, 3] = 255
    return bgra.tobytes()


def synthetic_bmp():
    x = np.arange(WIDTH, dtype=np.uint32)[None, :]
    y = np.arange(HEIGHT, dtype=np.uint32)[:, None]
    rgb = np.empty((HEIGHT, WIDTH, 3), dtype=np.uint8)
    rgb[:, :, 0] = (x * 3 + y) % 256
    rgb[:, :, 1] = (x + y * 5) % 256
    rgb[:, :, 2] = (x * 7 + y * 11) % 256
    return HEADER + rgb_to_bgra(rgb)


def synthetic_identity():
    return {SYNTHETIC_REFERENCE: hashlib.sha256(synthetic_bmp()).hexdigest()}


def prepare(rgb):
    if rgb.shape != (HEIGHT, WIDTH, 3) or rgb.dtype != np.uint8:
        raise ValueError('bad input image')
    padded = np.pad(rgb, ((2, 2), (0, 0), (0, 0)), mode='edge')
    array = padded.astype(np.float32) / np.float32(127.5) - np.float32(1)
    return np.transpose(array, (2, 0, 1)).copy()[None, ...]


def quantize(raw):
    if raw.shape != (1, 3, 544, 960) or raw.dtype != np.float32 or not np.isfinite(raw).all():
        raise ValueError('non-finite or unexpected model output')
    if float(raw.min()) < -1.00001 or float(raw.max()) > 1.00001:
        raise ValueError('model output range')
    arr = raw[0, :, 2:542, :].transpose(1, 2, 0)
    return rgb_to_bgra(np.clip((arr + 1) * 127.5, 0, 255).astype(np.uint8))


def load_model(model_path, device):
    # Hash before deserializing. Only the original FP32 checkpoint is accepted.
    if sha(model_path) != CHECKPOINT:
        raise ValueError('checkpoint hash differs')
    import torch
    from torch import nn
    import generator
    torch.set_num_threads(4)
    torch.set_num_interop_threads(1)
    if device != 'cuda' or not torch.cuda.is_available():
        raise RuntimeError('PyTorch CUDA/HIP GPU unavailable')
    checkpoint = torch.load(model_path, map_location='cpu', weights_only=True)
    if not isinstance(checkpoint, dict):
        raise TypeError('checkpoint is not a state dict')
    model = generator.GlobalGenerator(3, 3, ngf=64, n_downsampling=4, n_blocks=9,
                                     norm_layer=functools.partial(nn.InstanceNorm2d, affine=False))
    expected = model.state_dict()
    if set(expected) != set(checkpoint):
        raise ValueError('checkpoint key mismatch')
    for key, expected_tensor in expected.items():
        t = checkpoint[key]
        if (not torch.is_tensor(t) or t.shape != expected_tensor.shape or t.dtype != expected_tensor.dtype
                or not bool(torch.isfinite(t).all().item())):
            raise ValueError('checkpoint shape/dtype/finite mismatch: ' + key)
    model.load_state_dict(checkpoint, strict=True)
    model.eval().to(device)
    torch.cuda.synchronize()
    return model


def infer(model, rgb, device):
    import torch
    start = time.monotonic()
    tensor = torch.from_numpy(prepare(rgb))
    with torch.inference_mode():
        raw = model(tensor.to(device)).detach().to('cpu')
        torch.cuda.synchronize()
    array = raw.numpy()
    pixels = quantize(array)
    return array, pixels, {'total_worker_ms': (time.monotonic() - start) * 1000}


def compare(raw, pixels, reference):
    old = np.load(reference / 'raw-024.npy', allow_pickle=False)
    bmp = (reference / 'output-24.bmp').read_bytes()
    bmp_to_rgb(bmp)  # validate exact BMP header/length, including top-down orientation
    if old.shape != raw.shape or old.dtype != np.float32 or not np.isfinite(old).all():
        raise ValueError('reference raw invalid')
    if len(pixels) != PIXELS:
        raise ValueError('candidate pixel length')
    delta = np.abs(raw - old)
    pixel_delta = np.abs(np.frombuffer(pixels, dtype=np.uint8).astype(np.int16)
                         - np.frombuffer(bmp[54:], dtype=np.uint8).astype(np.int16))
    result = {'raw_max_abs': float(delta.max()), 'bgra_max_abs': int(pixel_delta.max()),
              'bgra_different_bytes': int(np.count_nonzero(pixel_delta))}
    result['matches'] = result['raw_max_abs'] <= 1e-4 and result['bgra_different_bytes'] == 0
    return result
