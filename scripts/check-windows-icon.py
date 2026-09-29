"""Validate the Windows logo without building or installing image libraries."""
import argparse
from pathlib import Path
import struct
import zlib

SIZES = {16, 24, 32, 48, 64, 128, 256}
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def check_icon(data):
    require(len(data) >= 6, "Truncated ICO header")
    reserved, kind, count = struct.unpack_from("<HHH", data)
    require((reserved, kind) == (0, 1) and count > 0, "Invalid ICO header")
    end = 6 + count * 16
    require(end <= len(data), "Truncated ICO directory")
    sizes = set()
    for index in range(count):
        w, h, colors, reserved, planes, bits, size, offset = struct.unpack_from(
            "<BBBBHHII", data, 6 + index * 16)
        w, h = w or 256, h or 256
        require(w == h and w not in sizes, "Non-square or duplicate ICO frame")
        require((colors, reserved, planes, bits) == (0, 0, 1, 32),
                f"Invalid {w}px directory entry")
        require(offset == end and size > 0 and offset + size <= len(data),
                f"Invalid {w}px frame bounds")
        frame = data[offset:offset + size]
        end = offset + size
        sizes.add(w)
        if frame.startswith(PNG_SIGNATURE):
            require(len(frame) >= 33 and frame[12:16] == b"IHDR",
                    "Missing PNG header")
            width, height, depth, color, compression, filtering, interlace = (
                struct.unpack_from(">IIBBBBB", frame, 16))
            require((width, height) == (w, h),
                    f"ICO declares {w}x{h}, but PNG contains {width}x{height}")
            require(w == 256 and (depth, color, compression, filtering, interlace)
                    == (8, 6, 0, 0, 0), "Expected a 256px RGBA PNG")
            position, compressed, ended = 8, bytearray(), False
            while position < len(frame):
                require(position + 12 <= len(frame), "Truncated PNG chunk")
                length = struct.unpack_from(">I", frame, position)[0]
                chunk_end = position + 8 + length
                require(chunk_end + 4 <= len(frame), "Invalid PNG chunk bounds")
                chunk = frame[position + 4:chunk_end]
                crc = struct.unpack_from(">I", frame, chunk_end)[0]
                require(zlib.crc32(chunk) == crc, "PNG checksum mismatch")
                if chunk[:4] == b"IDAT":
                    compressed.extend(chunk[4:])
                if chunk[:4] == b"IEND":
                    require(length == 0 and chunk_end + 4 == len(frame),
                            "Invalid PNG ending")
                    ended = True
                position = chunk_end + 4
            require(ended, "Missing PNG ending")
            pixels = zlib.decompress(compressed)
            require(len(pixels) == h * (1 + w * 4), "Invalid PNG pixel data")
        else:
            require(len(frame) >= 40, "Truncated DIB header")
            header, width, height, planes, bits, compression = struct.unpack_from(
                "<IiiHHI", frame)
            require((header, width, height, planes, bits, compression) ==
                    (40, w, h * 2, 1, 32, 0), "Invalid ICO DIB header")
            require(w < 256, "Use PNG for the 256px frame")
            stride = ((w + 31) // 32) * 4
            require(len(frame) == 40 + w * h * 4 + stride * h,
                    "Truncated DIB pixels or AND mask")
            alpha = frame[43:40 + w * h * 4:4]
            require(any(alpha), f"Entirely transparent {w}px frame")
            mask = frame[40 + w * h * 4:]
            for y in range(h):
                for x in range(w):
                    transparent = bool(mask[y * stride + x // 8] & (0x80 >> (x % 8)))
                    require(transparent == (alpha[y * w + x] == 0),
                            "DIB alpha and AND mask disagree")
    require(end == len(data), "Unexpected trailing ICO data")
    require(sizes == SIZES, f"Expected sizes {sorted(SIZES)}, found {sorted(sizes)}")
    return sorted(sizes)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", nargs="?", type=Path,
                        default=Path(__file__).resolve().parents[1] /
                        "resources/icons/icon.ico")
    args = parser.parse_args()
    try:
        sizes = check_icon(args.path.read_bytes())
    except (OSError, ValueError, struct.error, zlib.error) as error:
        parser.exit(1, f"Windows icon validation failed: {error}\n")
    print(f"Windows icon validated: {sizes}")
