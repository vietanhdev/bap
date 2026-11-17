// Sends "/" and locale-less paths to the visitor's language (cookie, then Accept-Language),
// and remembers the chosen locale in a cookie. Self-contained: no runtime dependencies.
import { NextResponse } from 'next/server'

const LOCALES = ['en', 'vi', 'ko', 'zh']
const DEFAULT_LOCALE = 'en'
const COOKIE = 'NEXT_LOCALE'
const HAS_LOCALE = new RegExp(`^/(${LOCALES.join('|')})(/|$)`)

function preferredLocale(request) {
  const cookie = request.cookies.get(COOKIE)?.value
  if (LOCALES.includes(cookie)) return cookie
  const header = request.headers.get('accept-language') || ''
  const ranked = header
    .split(',')
    .map(part => {
      const [tag, ...params] = part.trim().toLowerCase().split(';')
      const q = params.find(p => p.trim().startsWith('q='))
      return { lang: tag.split('-')[0], q: q ? Number(q.split('=')[1]) || 0 : 1 }
    })
    .filter(x => x.lang)
    .sort((a, b) => b.q - a.q)
  return ranked.find(x => LOCALES.includes(x.lang))?.lang || DEFAULT_LOCALE
}

export function middleware(request) {
  const { pathname, search } = request.nextUrl
  const match = pathname.match(HAS_LOCALE)
  if (!match) {
    const url = request.nextUrl.clone()
    url.pathname = `/${preferredLocale(request)}${pathname === '/' ? '' : pathname}`
    url.search = search
    return NextResponse.redirect(url)
  }
  if (request.cookies.get(COOKIE)?.value !== match[1]) {
    const response = NextResponse.next()
    response.cookies.set(COOKIE, match[1], { path: '/', maxAge: 60 * 60 * 24 * 365, sameSite: 'lax' })
    return response
  }
}

export const config = {
  matcher: [
    '/((?!api|_next/static|_next/image|favicon.ico|icon.png|apple-icon.png|og.png|robots.txt|sitemap.xml|screens|diagrams|web|faces|demo|photos|_pagefind).*)'
  ]
}
