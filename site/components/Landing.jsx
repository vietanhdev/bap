import Link from 'next/link'
import { REPO_URL, t } from '@/lib/i18n'

const CHARACTERS = [
  { id: 'bong', name: 'Bông', color: '#DCEBFF' },
  { id: 'bap', name: 'Bắp', color: '#FFD84D' },
  { id: 'bo', name: 'Bơ', color: '#C9E084' },
  { id: 'mochi', name: 'Mochi', color: '#FF96B4' },
  { id: 'ti', name: 'Tí', color: '#7EC8F0' }
]

const GALLERY = [
  ['listening', 'listening'],
  ['talking', 'subtitle'],
  ['music', 'music-lyrics'],
  ['radio', 'radio'],
  ['menu', 'menu'],
  ['settings', 'menu-settings'],
  ['korean', 'korean'],
  ['chinese', 'chinese']
]

const WEB = ['web-chat', 'web-me', 'web-sys', 'web-health']

export function Landing({ lang }) {
  const d = t(lang)
  return (
    <div className="bap-landing">
      <section className="bap-hero">
        <div className="bap-glow" aria-hidden="true" />
        <span className="bap-badge">{d.hero.badge}</span>
        <h1>{d.hero.title}</h1>
        <p className="bap-lead">{d.hero.lead}</p>
        <div className="bap-actions">
          <Link className="bap-btn bap-btn-primary" href={`/${lang}/getting-started`}>
            {d.hero.start} →
          </Link>
          <a className="bap-btn" href={REPO_URL} target="_blank" rel="noreferrer">
            {d.hero.github}
          </a>
        </div>
        <div className="bap-family">
          {CHARACTERS.map((c, i) => (
            <figure key={c.id} style={{ '--i': i }}>
              <img src={`/screens/idle-${c.id}.png`} alt={c.name} width={300} height={300} />
              <figcaption>
                <i style={{ background: c.color }} />
                {c.name}
              </figcaption>
            </figure>
          ))}
        </div>
        <p className="bap-norelay">
          <span aria-hidden="true">🔒</span> {d.hero.noRelay}
        </p>
      </section>

      <section className="bap-stats">
        {d.stats.map(([n, label]) => (
          <div key={label}>
            <b>{n}</b>
            <span>{label}</span>
          </div>
        ))}
      </section>

      <section className="bap-section">
        <h2>{d.demoTitle}</h2>
        <p className="bap-sub">{d.demoLead}</p>
        <div className="bap-demo">
          {[['photo', '/photos/in-hand.jpg'], ['gif', '/demo/conversation.gif'], ['gif', '/demo/characters.gif'], ['gif', '/demo/menu.gif']].map(([kind, src], i) => (
            <figure key={src} className={kind === 'photo' ? 'bap-demo-photo' : undefined}>
              <img src={src} alt={d.demo[i]} />
              <figcaption>{d.demo[i]}</figcaption>
            </figure>
          ))}
        </div>
      </section>

      <section className="bap-section">
        <h2>{d.featuresTitle}</h2>
        <div className="bap-features">
          {d.features.map(([icon, title, body]) => (
            <article key={title}>
              <span className="bap-ficon" aria-hidden="true">
                {icon}
              </span>
              <h3>{title}</h3>
              <p>{body}</p>
            </article>
          ))}
        </div>
      </section>

      <section className="bap-section">
        <h2>{d.galleryTitle}</h2>
        <p className="bap-sub">{d.galleryLead}</p>
        <div className="bap-gallery">
          {GALLERY.map(([key, file]) => (
            <figure key={key}>
              <img src={`/screens/${file}.png`} alt={d.gallery[key]} loading="lazy" width={300} height={300} />
              <figcaption>{d.gallery[key]}</figcaption>
            </figure>
          ))}
        </div>
      </section>

      <section className="bap-section">
        <h2>{d.webTitle}</h2>
        <div className="bap-web">
          {WEB.map((file, i) => (
            <figure key={file}>
              <img src={`/web/${file}.png`} alt={d.web[i]} loading="lazy" width={420} height={900} />
              <figcaption>{d.web[i]}</figcaption>
            </figure>
          ))}
        </div>
      </section>

      <section className="bap-section">
        <h2>{d.howTitle}</h2>
        <p className="bap-sub">{d.howLead}</p>
        <img className="bap-diagram" src="/diagrams/architecture.svg" alt={d.howTitle} loading="lazy" />
      </section>

      <section className="bap-cta">
        <img src="/faces/bap.png" alt="" width={84} height={84} />
        <h2>{d.ctaTitle}</h2>
        <p>{d.ctaLead}</p>
        <Link className="bap-btn bap-btn-primary" href={`/${lang}/getting-started`}>
          {d.hero.start} →
        </Link>
      </section>
    </div>
  )
}
