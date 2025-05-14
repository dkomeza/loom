import { draw_h_line, draw_rect, draw_v_line } from "./rendering/primitive";
import { buffer, width, height, init_buffer } from "./rendering/utils";
import "./style.css";

const canvas = document.querySelector("canvas") as HTMLCanvasElement;

const ctx = canvas.getContext("2d");
if (!ctx) {
  throw new Error("Failed to get canvas context");
}

ctx.fillStyle = "white";

init_buffer(canvas.width, canvas.height);

// for (let i = 0; i < buffer.length; i++) {
//   const r = (i % width) / width;
//   const g = Math.floor(i / height) / height;
//   const b = 0.5;

//   const R = Math.floor(r * 31);
//   const G = g * 63; // 6 bits
//   const B = Math.floor(b * 31); // 5 bits

//   buffer[i] = (R << 11) | (G << 5) | B;
// }

function flush_buffer() {
  if (!ctx) {
    throw new Error("Failed to get canvas context");
  }

  const imageData = ctx.createImageData(canvas.width, canvas.height);
  for (let i = 0; i < buffer.length; i++) {
    const val = buffer[i];

    const r5 = (val >> 11) & 0x1f;
    const g6 = (val >> 5) & 0x3f;
    const b5 = val & 0x1f;

    const r8 = (r5 * 255) / 31;
    const g8 = (g6 * 255) / 63;
    const b8 = (b5 * 255) / 31;

    imageData.data[i * 4 + 0] = r8;
    imageData.data[i * 4 + 1] = g8;
    imageData.data[i * 4 + 2] = b8;
    imageData.data[i * 4 + 3] = 255;
  }
  ctx.putImageData(imageData, 0, 0);
}

function RGB565(r: number, g: number, b: number): number {
  const R = Math.floor((r / 255) * 31);
  const G = Math.floor((g / 255) * 63);
  const B = Math.floor((b / 255) * 31);

  return (R << 11) | (G << 5) | B;
}

draw_rect(70, 100, 100, 100, RGB565(255, 0, 0));
flush_buffer();
