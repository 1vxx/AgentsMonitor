import type { Mood } from '../types.ts'

// The mood as a soft orb of light: three blurred colours drifting inside a
// sphere, previewing what the round AMOLED display will show. It swirls while
// agents work, pulses while one waits for you and barely moves at rest; the
// colours glide from one mood to the next.

export const Orb = ({ mood, size = 120 }: { mood: Mood; size?: number }) => (
  <span className={`orb mood-${mood} orb-${mood}`} style={{ ['--s' as string]: `${size}px` }} aria-hidden>
    <span className="orb-ring" />
    <span className="orb-body">
      <i className="orb-b1" />
      <i className="orb-b2" />
      <i className="orb-b3" />
    </span>
  </span>
)
