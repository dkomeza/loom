import { buffer, height, width } from "./utils";

export function draw_h_line(
  x: number,
  y: number,
  length: number,
  color: number
) {
  for (let i = x; i < x + length; i++) {
    buffer[y * width + i] = color;
  }
}

export function draw_v_line(
  x: number,
  y: number,
  length: number,
  color: number
) {
  for (let i = y; i < y + length; i++) {
    buffer[i * width + x] = color;
  }
}

export function draw_rect(
  x: number,
  y: number,
  width: number,
  height: number,
  color: number
) {
  draw_h_line(x, y, width, color);
  draw_h_line(x, y + height - 1, width, color);
  draw_v_line(x, y, height, color);
  draw_v_line(x + width - 1, y, height, color);
}
