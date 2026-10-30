// Photos for the core (jpeg::decode), decoded by the browser. The core asks
// and wants an answer at once; the browser decodes asynchronously. So the
// first ask for a frame starts its decode, and the next frames' in the pack
// after it, and answers no; asked again once it is done, it copies the frame
// in. Decoded frames are kept, the least recently used let go past a limit.

const KEEP = 160  // about 70 MB of decoded frames
const AHEAD = 8

// A decoder for the theme data at base: a photo pack, whose frames it decodes
// ahead; any other data it only decodes as asked
export function jpegDecoder(mod, base, len) {
  const done = new Map()  // a frame's address in the module's memory -> its pixels, big-endian RGB565
  const pending = new Set()
  const frames = []       // each frame's address and length, in the pack's order
  const order = new Map() // a frame's address -> its place in frames
  const dv = new DataView(mod.HEAPU8.buffer, base, len)
  const magic = len > 24 ? String.fromCharCode(...mod.HEAPU8.subarray(base, base + 4)) : ''
  if (magic === 'FPK1' || magic === 'FPK2') {
    // Version 2's header has the text section's place too
    const at = (magic === 'FPK2' ? 24 : 16) + 20 * dv.getUint16(14, true)
    for (let i = 0, n = dv.getUint32(at, true); i < n; i++) {
      const p = base + dv.getUint32(at + 4 + 8 * i, true)
      order.set(p, frames.length)
      frames.push([p, dv.getUint32(at + 8 + 8 * i, true)])
    }
  }

  let ctx
  async function decode(p, n, w, h) {
    pending.add(p)
    try {
      const bitmap = await createImageBitmap(new Blob([mod.HEAPU8.slice(p, p + n)], { type: 'image/jpeg' }))
      if (!ctx || ctx.canvas.width !== w) ctx = new OffscreenCanvas(w, h).getContext('2d', { willReadFrequently: true })
      ctx.drawImage(bitmap, 0, 0)
      bitmap.close()
      const px = ctx.getImageData(0, 0, w, h).data
      const out = new Uint8Array(w * h * 2)
      for (let i = 0, j = 0; i < px.length; i += 4, j += 2) {
        out[j] = (px[i] & 0xf8) | (px[i + 1] >> 5)
        out[j + 1] = ((px[i + 1] & 0x1c) << 3) | (px[i + 2] >> 3)
      }
      done.set(p, out)
      if (done.size > KEEP) done.delete(done.keys().next().value)
    } catch {
      // Not a photo the browser can read: it stays undecoded, and asks stay no
    } finally {
      pending.delete(p)
    }
  }

  return (p, n, out, w, h) => {
    const k = order.get(p)
    if (k !== undefined)
      for (let i = 1; i <= AHEAD && k + i < frames.length; i++) {
        const [q, m] = frames[k + i]
        if (!done.has(q) && !pending.has(q)) decode(q, m, w, h)
      }
    const got = done.get(p)
    if (!got) {
      if (!pending.has(p)) decode(p, n, w, h)
      return 0
    }
    // Most recently used goes last, furthest from being let go
    done.delete(p)
    done.set(p, got)
    mod.HEAPU8.set(got, out)
    return 1
  }
}
