import { useId } from 'react'

/** Three signal bars on a clay tile; they keep time while agents are working. */
export const Logo = ({ live, size = 28 }: { live?: boolean; size?: number }) => {
  const id = useId().replace(/:/g, '')
  return (
    <svg className={live ? 'logo logo-live' : 'logo'} width={size} height={size} viewBox="0 0 32 32" aria-hidden>
      <defs>
        <linearGradient id={`logo${id}`} x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stopColor="#ee9a78" />
          <stop offset="0.55" stopColor="#d97757" />
          <stop offset="1" stopColor="#b9583a" />
        </linearGradient>
      </defs>
      <rect x="1" y="1" width="30" height="30" rx="9" fill={`url(#logo${id})`} />
      <rect className="logo-bar" x="8.6" y="11.5" width="3.6" height="9" rx="1.8" />
      <rect className="logo-bar" x="14.2" y="7.5" width="3.6" height="17" rx="1.8" />
      <rect className="logo-bar" x="19.8" y="10" width="3.6" height="12" rx="1.8" />
    </svg>
  )
}
