import { Footer, Layout, Navbar, LocaleSwitch } from 'nextra-theme-docs'
import { Head, Search } from 'nextra/components'
import { getPageMap } from 'nextra/page-map'
import { Nunito } from 'next/font/google'
import 'nextra-theme-docs/style.css'
import '../globals.css'
import { LOCALES, REPO_URL, SITE_URL, t } from '@/lib/i18n'

const nunito = Nunito({
  subsets: ['latin', 'latin-ext', 'vietnamese'],
  weight: ['400', '600', '700', '800', '900'],
  display: 'swap',
  variable: '--font-nunito'
})

export async function generateMetadata({ params }) {
  const { lang } = await params
  const d = t(lang)
  return {
    metadataBase: new URL(SITE_URL),
    title: { default: `Bắp · ${d.hero.title}`, template: '%s · Bắp' },
    description: d.description,
    applicationName: 'Bắp',
    alternates: {
      canonical: `/${lang}`,
      languages: Object.fromEntries(LOCALES.map(l => [l.locale, `/${l.locale}`]))
    },
    openGraph: {
      type: 'website',
      siteName: 'Bắp',
      title: `Bắp · ${d.hero.title}`,
      description: d.description,
      url: `/${lang}`,
      locale: lang,
      images: [{ url: '/og.png', width: 1200, height: 630, alt: 'Bắp characters' }]
    },
    twitter: { card: 'summary_large_image', images: ['/og.png'] }
  }
}

export function generateStaticParams() {
  return LOCALES.map(l => ({ lang: l.locale }))
}

function Logo() {
  return (
    <span className="bap-logo">
      <img src="/faces/bap.png" alt="" width={30} height={30} />
      <b>Bắp</b>
    </span>
  )
}

export default async function RootLayout({ children, params }) {
  const { lang } = await params
  const d = t(lang)
  const pageMap = await getPageMap(`/${lang}`)
  return (
    <html lang={lang} dir="ltr" suppressHydrationWarning className={nunito.variable}>
      <Head color={{ hue: 256, saturation: 100, lightness: { light: 62, dark: 72 } }} backgroundColor={{ dark: '#15131f', light: '#fbfaff' }} />
      <body>
        <Layout
          navbar={
            <Navbar logo={<Logo />} logoLink={`/${lang}`} projectLink={REPO_URL}>
              <LocaleSwitch lite />
            </Navbar>
          }
          footer={
            <Footer>
              <span className="bap-footer">
                <img src="/faces/bong.png" alt="" width={22} height={22} /> Bắp · {d.footer}
              </span>
            </Footer>
          }
          search={<Search placeholder={d.search} />}
          docsRepositoryBase={`${REPO_URL}/tree/main/site`}
          editLink={null}
          feedback={{ content: null }}
          i18n={LOCALES}
          pageMap={pageMap}
          sidebar={{ defaultMenuCollapseLevel: 1, toggleButton: true }}
          toc={{ title: d.toc, backToTop: d.backToTop }}
          nextThemes={{ defaultTheme: 'system' }}
        >
          {children}
        </Layout>
      </body>
    </html>
  )
}
