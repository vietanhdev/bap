import { SITE_URL } from '@/lib/i18n'

export default function robots() {
  return { rules: { userAgent: '*', allow: '/' }, sitemap: `${SITE_URL}/sitemap.xml` }
}
