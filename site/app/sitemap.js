import { LOCALES, SITE_URL } from '@/lib/i18n'
import { PAGES } from '@/lib/pages'

export default function sitemap() {
  const now = new Date()
  return LOCALES.flatMap(({ locale }) =>
    ['', ...PAGES].map(p => ({
      url: `${SITE_URL}/${locale}${p ? '/' + p : ''}`,
      lastModified: now,
      alternates: {
        languages: Object.fromEntries(LOCALES.map(l => [l.locale, `${SITE_URL}/${l.locale}${p ? '/' + p : ''}`]))
      }
    }))
  )
}
