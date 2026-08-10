// zen-test — Screenshot capture and analysis.

use crate::errors::{Result, ZenTestError};
use crate::output;
use crate::qmp::commands::qmp_screendump;
use serde::Serialize;
use std::path::Path;

/// Screenshot metadata.
#[derive(Debug, Serialize)]
pub struct ScreenshotInfo {
    pub path: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub png_path: Option<String>,
    pub size_bytes: u64,
    pub is_blank: bool,
    pub width: u32,
    pub height: u32,
}

/// Region analysis metadata for stronger screenshot assertions.
#[derive(Debug, Serialize)]
pub struct ScreenshotRegionInfo {
    pub x: u32,
    pub y: u32,
    pub width: u32,
    pub height: u32,
    pub is_blank: bool,
    pub non_black_pixels: u64,
}

/// Capture a screenshot via QMP screendump.
///
/// Writes to a temp file first, then renames to the target path (atomic).
pub async fn capture(
    qmp_socket: &Path,
    output_path: &Path,
) -> Result<ScreenshotInfo> {
    // Ensure parent directory exists.
    if let Some(parent) = output_path.parent() {
        std::fs::create_dir_all(parent)?;
    }

    // QMP screendump writes directly to the path.
    // We write to a .tmp first for atomicity.
    let tmp_path = output_path.with_extension("ppm.tmp");

    qmp_screendump(qmp_socket, &tmp_path).await.map_err(|e| {
        ZenTestError::ScreenshotFailed(format!("QMP screendump failed: {e}"))
    })?;

    // Brief wait for QEMU to write the file.
    tokio::time::sleep(std::time::Duration::from_millis(500)).await;

    if !tmp_path.exists() {
        return Err(ZenTestError::ScreenshotFailed(
            "Screenshot file not created by QEMU".into(),
        ));
    }

    // Rename to final path.
    std::fs::rename(&tmp_path, output_path)?;

    let metadata = std::fs::metadata(output_path)?;
    let parsed = parse_ppm(output_path)?;
    let is_blank = check_blank_pixels(&parsed.pixels);
    let png_path = output_path.with_extension("png");
    write_png(&png_path, parsed.width, parsed.height, &parsed.pixels)?;

    output::log_ok(&format!(
        "Screenshot saved: {} ({} bytes, blank: {is_blank})",
        output_path.display(),
        metadata.len()
    ));

    Ok(ScreenshotInfo {
        path: output_path.display().to_string(),
        png_path: Some(png_path.display().to_string()),
        size_bytes: metadata.len(),
        is_blank,
        width: parsed.width,
        height: parsed.height,
    })
}

/// Analyze a rectangular region inside a PPM screenshot.
pub fn analyze_region(
    ppm_path: &Path,
    x: u32,
    y: u32,
    width: u32,
    height: u32,
) -> Result<ScreenshotRegionInfo> {
    let parsed = parse_ppm(ppm_path)?;

    if width == 0 || height == 0 {
        return Err(ZenTestError::ScreenshotFailed(
            "Screenshot region must have non-zero width and height".into(),
        ));
    }

    let end_x = x.checked_add(width).ok_or_else(|| {
        ZenTestError::ScreenshotFailed("Screenshot region x overflows".into())
    })?;
    let end_y = y.checked_add(height).ok_or_else(|| {
        ZenTestError::ScreenshotFailed("Screenshot region y overflows".into())
    })?;

    if end_x > parsed.width || end_y > parsed.height {
        return Err(ZenTestError::ScreenshotFailed(format!(
            "Screenshot region out of bounds: image={}x{}, region=({},{} {}x{})",
            parsed.width, parsed.height, x, y, width, height
        )));
    }

    let mut first_pixel: Option<[u8; 3]> = None;
    let mut is_blank = true;
    let mut non_black_pixels = 0u64;

    for row in y..end_y {
        let row_start = ((row * parsed.width + x) * 3) as usize;
        let row_end = ((row * parsed.width + end_x) * 3) as usize;
        let row_pixels = &parsed.pixels[row_start..row_end];

        for chunk in row_pixels.chunks_exact(3) {
            let pixel = [chunk[0], chunk[1], chunk[2]];

            if pixel != [0, 0, 0] {
                non_black_pixels += 1;
            }

            match first_pixel {
                Some(first) if first != pixel => is_blank = false,
                None => first_pixel = Some(pixel),
                _ => {}
            }
        }
    }

    Ok(ScreenshotRegionInfo {
        x,
        y,
        width,
        height,
        is_blank,
        non_black_pixels,
    })
}

struct ParsedPpm {
    width: u32,
    height: u32,
    pixels: Vec<u8>,
}

fn write_png(path: &Path, width: u32, height: u32, pixels: &[u8]) -> Result<()> {
    let mut raw = Vec::with_capacity((width as usize * 3 + 1) * height as usize);
    let row_bytes = width as usize * 3;
    for row in 0..height as usize {
        raw.push(0);
        let start = row * row_bytes;
        raw.extend_from_slice(&pixels[start..start + row_bytes]);
    }

    let mut png = Vec::new();
    png.extend_from_slice(&[137, 80, 78, 71, 13, 10, 26, 10]);

    let mut ihdr = Vec::with_capacity(13);
    ihdr.extend_from_slice(&width.to_be_bytes());
    ihdr.extend_from_slice(&height.to_be_bytes());
    ihdr.push(8);
    ihdr.push(2);
    ihdr.push(0);
    ihdr.push(0);
    ihdr.push(0);
    write_png_chunk(&mut png, b"IHDR", &ihdr);

    let zlib = zlib_store_compress(&raw);
    write_png_chunk(&mut png, b"IDAT", &zlib);
    write_png_chunk(&mut png, b"IEND", &[]);

    std::fs::write(path, png)?;
    Ok(())
}

fn write_png_chunk(buf: &mut Vec<u8>, chunk_type: &[u8; 4], data: &[u8]) {
    buf.extend_from_slice(&(data.len() as u32).to_be_bytes());
    buf.extend_from_slice(chunk_type);
    buf.extend_from_slice(data);

    let mut crc_input = Vec::with_capacity(4 + data.len());
    crc_input.extend_from_slice(chunk_type);
    crc_input.extend_from_slice(data);
    buf.extend_from_slice(&crc32(&crc_input).to_be_bytes());
}

fn zlib_store_compress(data: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(data.len() + data.len() / 65535 * 5 + 16);
    out.extend_from_slice(&[0x78, 0x01]);

    let mut offset = 0usize;
    while offset < data.len() {
        let remaining = data.len() - offset;
        let block_len = remaining.min(65535);
        let final_block = offset + block_len >= data.len();
        out.push(if final_block { 0x01 } else { 0x00 });
        let len = block_len as u16;
        let nlen = !len;
        out.extend_from_slice(&len.to_le_bytes());
        out.extend_from_slice(&nlen.to_le_bytes());
        out.extend_from_slice(&data[offset..offset + block_len]);
        offset += block_len;
    }

    out.extend_from_slice(&adler32(data).to_be_bytes());
    out
}

fn adler32(data: &[u8]) -> u32 {
    const MOD: u32 = 65521;
    let mut s1 = 1u32;
    let mut s2 = 0u32;

    for byte in data {
        s1 = (s1 + *byte as u32) % MOD;
        s2 = (s2 + s1) % MOD;
    }

    (s2 << 16) | s1
}

fn crc32(data: &[u8]) -> u32 {
    let mut crc = 0xffff_ffffu32;
    for byte in data {
        crc ^= *byte as u32;
        for _ in 0..8 {
            let mask = (crc & 1).wrapping_neg() & 0xedb8_8320;
            crc = (crc >> 1) ^ mask;
        }
    }
    !crc
}

fn parse_ppm(ppm_path: &Path) -> Result<ParsedPpm> {
    let data = std::fs::read(ppm_path).map_err(|e| {
        ZenTestError::ScreenshotFailed(format!(
            "Failed to read screenshot {}: {e}",
            ppm_path.display()
        ))
    })?;

    let mut idx = 0usize;
    let magic = next_ppm_token(&data, &mut idx).ok_or_else(|| {
        ZenTestError::ScreenshotFailed("Missing PPM magic header".into())
    })?;
    if magic != "P6" {
        return Err(ZenTestError::ScreenshotFailed(format!(
            "Unsupported PPM magic header: {magic}"
        )));
    }

    let width = next_ppm_token(&data, &mut idx)
        .ok_or_else(|| ZenTestError::ScreenshotFailed("Missing PPM width".into()))?
        .parse::<u32>()
        .map_err(|e| ZenTestError::ScreenshotFailed(format!("Invalid PPM width: {e}")))?;

    let height = next_ppm_token(&data, &mut idx)
        .ok_or_else(|| ZenTestError::ScreenshotFailed("Missing PPM height".into()))?
        .parse::<u32>()
        .map_err(|e| ZenTestError::ScreenshotFailed(format!("Invalid PPM height: {e}")))?;

    let max_value = next_ppm_token(&data, &mut idx)
        .ok_or_else(|| ZenTestError::ScreenshotFailed("Missing PPM max value".into()))?;
    if max_value != "255" {
        return Err(ZenTestError::ScreenshotFailed(format!(
            "Unsupported PPM max value: {max_value}"
        )));
    }

    while idx < data.len() && data[idx].is_ascii_whitespace() {
        idx += 1;
    }

    let expected_pixels = width
        .checked_mul(height)
        .and_then(|px| px.checked_mul(3))
        .ok_or_else(|| ZenTestError::ScreenshotFailed("PPM dimensions overflow".into()))?
        as usize;

    if data.len() < idx + expected_pixels {
        return Err(ZenTestError::ScreenshotFailed(format!(
            "PPM pixel data truncated: expected {expected_pixels} bytes, got {}",
            data.len().saturating_sub(idx)
        )));
    }

    Ok(ParsedPpm {
        width,
        height,
        pixels: data[idx..idx + expected_pixels].to_vec(),
    })
}

fn next_ppm_token(data: &[u8], idx: &mut usize) -> Option<String> {
    while *idx < data.len() {
        let byte = data[*idx];
        if byte == b'#' {
            while *idx < data.len() && data[*idx] != b'\n' {
                *idx += 1;
            }
        } else if byte.is_ascii_whitespace() {
            *idx += 1;
        } else {
            break;
        }
    }

    if *idx >= data.len() {
        return None;
    }

    let start = *idx;
    while *idx < data.len() && !data[*idx].is_ascii_whitespace() {
        *idx += 1;
    }

    Some(String::from_utf8_lossy(&data[start..*idx]).to_string())
}

fn check_blank_pixels(pixels: &[u8]) -> bool {
    if pixels.len() < 6 {
        return true;
    }

    let first_r = pixels[0];
    let first_g = pixels[1];
    let first_b = pixels[2];

    let step = std::cmp::max(3000, 3);
    for chunk in pixels.chunks(step) {
        if chunk.len() < 3 {
            break;
        }
        if chunk[0] != first_r || chunk[1] != first_g || chunk[2] != first_b {
            return false;
        }
    }

    true
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn write_test_ppm(rgb_rows: &[&[[u8; 3]]]) -> std::path::PathBuf {
        let height = rgb_rows.len();
        let width = rgb_rows[0].len();
        let mut bytes = format!("P6\n{} {}\n255\n", width, height).into_bytes();
        for row in rgb_rows {
            for pixel in *row {
                bytes.extend_from_slice(pixel);
            }
        }

        let unique = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_nanos();
        let path = std::env::temp_dir().join(format!("zen-test-screenshot-{unique}.ppm"));
        std::fs::write(&path, bytes).unwrap();
        path
    }

    #[test]
    fn analyze_region_counts_non_black_pixels() {
        let black = [0, 0, 0];
        let white = [255, 255, 255];
        let path = write_test_ppm(&[
            &[black, black, black, black],
            &[black, white, white, black],
            &[black, white, white, black],
            &[black, black, black, black],
        ]);

        let region = analyze_region(&path, 1, 1, 2, 2).unwrap();
        assert!(region.is_blank);
        assert_eq!(region.non_black_pixels, 4);

        let _ = std::fs::remove_file(path);
    }

    #[test]
    fn analyze_region_rejects_out_of_bounds_region() {
        let black = [0, 0, 0];
        let path = write_test_ppm(&[
            &[black, black],
            &[black, black],
        ]);

        let err = analyze_region(&path, 1, 1, 2, 2).unwrap_err();
        assert!(err.to_string().contains("out of bounds"));

        let _ = std::fs::remove_file(path);
    }

    #[test]
    fn writes_png_with_valid_signature() {
        let black = [0, 0, 0];
        let white = [255, 255, 255];
        let ppm = write_test_ppm(&[
            &[black, white],
            &[white, black],
        ]);
        let png = ppm.with_extension("png");
        let parsed = parse_ppm(&ppm).unwrap();
        write_png(&png, parsed.width, parsed.height, &parsed.pixels).unwrap();

        let bytes = std::fs::read(&png).unwrap();
        assert_eq!(&bytes[..8], &[137, 80, 78, 71, 13, 10, 26, 10]);

        let _ = std::fs::remove_file(ppm);
        let _ = std::fs::remove_file(png);
    }
}
