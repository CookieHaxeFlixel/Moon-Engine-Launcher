import os
import struct
import zlib

root = os.path.dirname(os.path.abspath(__file__))
icon_dir = os.path.join(root, 'assets', 'app', 'icons')
out_path = os.path.join(icon_dir, 'app.ico')
icon_sizes = (256, 128, 64, 48, 32, 16)


def paeth_predictor(left, above, upper_left):
    estimate = left + above - upper_left
    left_distance = abs(estimate - left)
    above_distance = abs(estimate - above)
    corner_distance = abs(estimate - upper_left)

    if left_distance <= above_distance and left_distance <= corner_distance:
        return left
    if above_distance <= corner_distance:
        return above
    return upper_left


def read_png(path):
    with open(path, 'rb') as source:
        data = source.read()

    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'Not a PNG file: {path}')

    width = height = bit_depth = color_type = interlace = None
    compressed = bytearray()
    palette = b''
    transparency = b''
    offset = 8

    while offset < len(data):
        length = struct.unpack_from('>I', data, offset)[0]
        chunk_type = data[offset + 4:offset + 8]
        chunk = data[offset + 8:offset + 8 + length]
        offset += length + 12

        if chunk_type == b'IHDR':
            width, height, bit_depth, color_type, _, _, interlace = struct.unpack(
                '>IIBBBBB', chunk
            )
        elif chunk_type == b'IDAT':
            compressed.extend(chunk)
        elif chunk_type == b'PLTE':
            palette = chunk
        elif chunk_type == b'tRNS':
            transparency = chunk
        elif chunk_type == b'IEND':
            break

    channel_counts = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}
    if bit_depth != 8 or color_type not in channel_counts or interlace != 0:
        raise ValueError(f'Unsupported PNG format: {path}')

    channels = channel_counts[color_type]
    bytes_per_pixel = channels
    stride = width * channels
    decoded = zlib.decompress(compressed)
    rows = []
    previous = bytearray(stride)
    source_offset = 0

    for _ in range(height):
        filter_type = decoded[source_offset]
        source_offset += 1
        row = bytearray(decoded[source_offset:source_offset + stride])
        source_offset += stride

        for index in range(stride):
            left = row[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
            above = previous[index]
            upper_left = previous[index - bytes_per_pixel] if index >= bytes_per_pixel else 0

            if filter_type == 1:
                row[index] = (row[index] + left) & 0xFF
            elif filter_type == 2:
                row[index] = (row[index] + above) & 0xFF
            elif filter_type == 3:
                row[index] = (row[index] + ((left + above) // 2)) & 0xFF
            elif filter_type == 4:
                row[index] = (
                    row[index] + paeth_predictor(left, above, upper_left)
                ) & 0xFF
            elif filter_type != 0:
                raise ValueError(f'Unsupported PNG filter {filter_type}: {path}')

        rows.append(row)
        previous = row

    pixels = []
    for row in rows:
        rgba_row = []
        for x in range(width):
            start = x * channels

            if color_type == 6:
                red, green, blue, alpha = row[start:start + 4]
            elif color_type == 2:
                red, green, blue = row[start:start + 3]
                alpha = 255
                if transparency and (red, green, blue) == struct.unpack('>HHH', transparency):
                    alpha = 0
            elif color_type == 0:
                red = green = blue = row[start]
                alpha = 0 if transparency and row[start] == transparency[1] else 255
            elif color_type == 4:
                red = green = blue = row[start]
                alpha = row[start + 1]
            else:
                palette_index = row[start]
                palette_start = palette_index * 3
                red, green, blue = palette[palette_start:palette_start + 3]
                alpha = transparency[palette_index] if palette_index < len(transparency) else 255

            rgba_row.append((red, green, blue, alpha))
        pixels.append(rgba_row)

    return width, height, pixels


def make_icon_image(width, height, pixels):
    xor_bitmap = bytearray()
    and_mask = bytearray()
    mask_stride = ((width + 31) // 32) * 4

    for row in reversed(pixels):
        for red, green, blue, alpha in row:
            xor_bitmap.extend((blue, green, red, alpha))

        mask_row = bytearray(mask_stride)
        for x, (_, _, _, alpha) in enumerate(row):
            if alpha < 128:
                mask_row[x // 8] |= 1 << (7 - (x % 8))
        and_mask.extend(mask_row)

    dib_header = struct.pack(
        '<IiiHHIIiiII',
        40,
        width,
        height * 2,
        1,
        32,
        0,
        len(xor_bitmap),
        0,
        0,
        0,
        0,
    )
    return dib_header + xor_bitmap + and_mask


images = []
for size in icon_sizes:
    png_path = os.path.join(icon_dir, f'iconMoon-{size}x{size}.png')
    if not os.path.isfile(png_path):
        continue

    width, height, pixels = read_png(png_path)
    images.append((width, height, make_icon_image(width, height, pixels)))

if not images:
    raise SystemExit('No iconMoon PNG files found')

directory = bytearray(struct.pack('<HHH', 0, 1, len(images)))
image_offset = 6 + 16 * len(images)

for width, height, image_data in images:
    directory.extend(struct.pack(
        '<BBBBHHII',
        0 if width == 256 else width,
        0 if height == 256 else height,
        0,
        0,
        1,
        32,
        len(image_data),
        image_offset,
    ))
    image_offset += len(image_data)

with open(out_path, 'wb') as output:
    output.write(directory)
    for _, _, image_data in images:
        output.write(image_data)

print(f'ICO created from project PNG assets: {out_path}')
