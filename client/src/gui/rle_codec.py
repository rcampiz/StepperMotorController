"""
PackBits-style RLE encoder for RGB565 pixel data.

Encoding format (operates on pixel units, 2 bytes each):
  Header byte:
    0x00-0x7F (N): Literal run -- next (N+1) raw pixels follow
    0x80-0xFF (N): Repeat run  -- next single pixel repeated (N-125) times

Minimum repeat threshold of 3 ensures RLE never expands data.
"""


def encode_rle_rgb565(data: bytes) -> bytes:
    """Encode RGB565 pixel data using PackBits-style RLE.

    Args:
        data: Raw RGB565 bytes (2 bytes per pixel, big-endian).
              Length must be even.

    Returns:
        RLE-compressed bytes.
    """
    if len(data) % 2 != 0:
        raise ValueError("Data length must be even (2 bytes per pixel)")

    num_pixels = len(data) // 2
    if num_pixels == 0:
        return b""

    # Extract pixel values as list of 2-byte tuples
    pixels = []
    for i in range(0, len(data), 2):
        pixels.append((data[i], data[i + 1]))

    result = bytearray()
    i = 0

    while i < num_pixels:
        # Look ahead for repeat run
        run_start = i
        while (i + 1 < num_pixels and
               pixels[i] == pixels[i + 1] and
               i - run_start < 129):  # max repeat = 130
            i += 1

        run_len = i - run_start + 1

        if run_len >= 3:
            # Emit repeat run
            header = run_len + 125  # 3->128 (0x80), 130->255 (0xFF)
            result.append(header)
            result.append(pixels[run_start][0])
            result.append(pixels[run_start][1])
            i = run_start + run_len
        else:
            # Collect literal run
            i = run_start
            lit_start = i
            lit_pixels = []

            while i < num_pixels and len(lit_pixels) < 128:
                # Check if a repeat run of 3+ starts here
                if (i + 2 < num_pixels and
                        pixels[i] == pixels[i + 1] == pixels[i + 2]):
                    break
                lit_pixels.append(pixels[i])
                i += 1

            if not lit_pixels:
                # Edge case: we're at a repeat run start, handled next iteration
                continue

            # Emit literal run
            header = len(lit_pixels) - 1  # 1->0x00, 128->0x7F
            result.append(header)
            for px in lit_pixels:
                result.append(px[0])
                result.append(px[1])

    return bytes(result)


def decode_rle_rgb565(data: bytes, expected_pixels: int) -> bytes:
    """Decode PackBits-style RLE back to raw RGB565 data.

    Args:
        data: RLE-compressed bytes.
        expected_pixels: Expected number of output pixels.

    Returns:
        Raw RGB565 bytes (2 bytes per pixel).

    Raises:
        ValueError: If decode produces wrong pixel count or data is malformed.
    """
    result = bytearray()
    i = 0

    while i < len(data):
        header = data[i]
        i += 1

        if header & 0x80:
            # Repeat run
            count = header - 125
            if i + 1 >= len(data):
                raise ValueError(f"Truncated repeat run at byte {i}")
            hi = data[i]
            lo = data[i + 1]
            i += 2
            for _ in range(count):
                result.append(hi)
                result.append(lo)
        else:
            # Literal run
            count = header + 1
            for _ in range(count):
                if i + 1 >= len(data):
                    raise ValueError(f"Truncated literal run at byte {i}")
                result.append(data[i])
                result.append(data[i + 1])
                i += 2

    if len(result) != expected_pixels * 2:
        raise ValueError(
            f"Decoded {len(result) // 2} pixels, expected {expected_pixels}"
        )

    return bytes(result)
