export let buffer: Uint16Array;

export let width: number;
export let height: number;

export function init_buffer(w: number, h: number) {
  width = w;
  height = h;
  buffer = new Uint16Array(width * height);
}
