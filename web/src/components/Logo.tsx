import { useId } from 'react'

/**
 * A clay planet on a tile, with a satellite on a tilted orbit; it goes round
 * while agents work. The tile, orbit and stars take the theme's colours
 * (--logo-* in tokens.css): night sky in dark, paper and ink in light.
 */
export const Logo = ({ live, size = 28 }: { live?: boolean; size?: number }) => {
  const id = useId().replace(/:/g, '')
  const orbit = 'M4.6,16a11.4,4.6 0 1 0 22.8,0a11.4,4.6 0 1 0 -22.8,0'
  return (
    <svg className={live ? 'logo logo-live' : 'logo'} width={size} height={size} viewBox="0 0 32 32" aria-hidden>
      <defs>
        <radialGradient id={`tile${id}`} cx="0.3" cy="0.2" r="1">
          <stop offset="0" className="logo-tile-a" />
          <stop offset="0.6" className="logo-tile-b" />
          <stop offset="1" className="logo-tile-c" />
        </radialGradient>
        <radialGradient id={`planet${id}`} cx="0.35" cy="0.3" r="0.8">
          <stop offset="0" stopColor="#ffd2b8" />
          <stop offset="0.45" stopColor="#ef8a66" />
          <stop offset="1" stopColor="#a8452a" />
        </radialGradient>
      </defs>
      <rect x="1" y="1" width="30" height="30" rx="9" fill={`url(#tile${id})`} />
      <rect className="logo-edge" x="1.5" y="1.5" width="29" height="29" rx="8.5" fill="none" />
      <circle className="logo-star" cx="8" cy="8.5" r="0.6" opacity="0.7" />
      <circle className="logo-star" cx="24.5" cy="25" r="0.5" opacity="0.5" />
      <circle className="logo-star" cx="25.5" cy="7" r="0.4" opacity="0.6" />
      <g transform="rotate(-24 16 16)">
        {/* The far half of the orbit passes behind the planet */}
        <path className="logo-orbit logo-orbit-back" d="M4.6,16a11.4,4.6 0 0 1 22.8,0" fill="none" strokeWidth="1" />
        <circle cx="16" cy="16" r="5.6" fill={`url(#planet${id})`} />
        <path className="logo-orbit" d="M27.4,16a11.4,4.6 0 0 1 -22.8,0" fill="none" strokeWidth="1.1" />
        <circle className="logo-sat" r="1.7" cx={live ? 0 : 27.4} cy={live ? 0 : 16}>
          {live && <animateMotion dur="2.6s" repeatCount="indefinite" path={orbit} />}
        </circle>
      </g>
    </svg>
  )
}
