// Character cards, from firmware/data/assistant.json (ids, names, voices, colours).
const DATA = [
  { id: 'bong', name: 'Bông', voice: 'Leda', shape: 'cloud' },
  { id: 'bap', name: 'Bắp', voice: 'Puck', shape: 'corn' },
  { id: 'bo', name: 'Bơ', voice: 'Achird', shape: 'avocado' },
  { id: 'mochi', name: 'Mochi', voice: 'Aoede', shape: 'mochi' },
  { id: 'ti', name: 'Tí', voice: 'Kore', shape: 'robot' }
]

const TEXT = {
  en: {
    bong: ['a little cotton cloud', 'Gentle, warm and encouraging (default)'],
    bap: ['a golden corn cob', 'Playful, energetic, crunchy chatter'],
    bo: ['a creamy avocado', 'Calm, relaxed, cares about your health'],
    mochi: ['a pink mochi', 'Sweet storyteller, great with kids'],
    ti: ['a tiny robot', 'Tidy and tech-savvy, an engineer’s helper']
  },
  vi: {
    bong: ['bé mây bông', 'Dịu dàng, ấm áp, hay động viên (mặc định)'],
    bap: ['trái bắp vàng', 'Vui nhộn, năng lượng, nói chuyện giòn tan'],
    bo: ['quả bơ sáp', 'Điềm đạm, thư thái, quan tâm sức khỏe'],
    mochi: ['viên bánh mochi hồng', 'Ngọt ngào, kể chuyện hay, hợp với trẻ nhỏ'],
    ti: ['chú robot tí hon', 'Gọn gàng, rành công nghệ, trợ lý cho dân kỹ thuật']
  },
  ko: {
    bong: ['작은 솜구름', '다정하고 따뜻하며 응원해 줘요 (기본)'],
    bap: ['노란 옥수수', '유쾌하고 에너지 넘치는 수다쟁이'],
    bo: ['부드러운 아보카도', '차분하고 느긋하며 건강을 챙겨요'],
    mochi: ['분홍 모찌', '달콤한 이야기꾼, 아이들과 잘 어울려요'],
    ti: ['꼬마 로봇', '깔끔하고 기술에 밝은 엔지니어의 조수']
  },
  zh: {
    bong: ['一朵小棉花云', '温柔、暖心、爱鼓励人（默认）'],
    bap: ['一根金黄玉米', '活泼、元气满满、说话脆生生'],
    bo: ['一颗牛油果', '沉稳、放松、关心你的健康'],
    mochi: ['一颗粉色麻糬', '甜美、会讲故事、适合小朋友'],
    ti: ['一个小机器人', '干练、懂技术，是工程师的好帮手']
  }
}

const VOICE = { en: 'Voice', vi: 'Giọng', ko: '음성', zh: '声音' }

export function Characters({ lang = 'en' }) {
  const text = TEXT[lang] || TEXT.en
  return (
    <div className="bap-chars">
      {DATA.map(c => (
        <article key={c.id}>
          <img src={`/faces/${c.id}.png`} alt={c.name} width={96} height={96} />
          <h3>{c.name}</h3>
          <small>
            <code>{c.id}</code> · {text[c.id][0]}
          </small>
          <p>{text[c.id][1]}</p>
          <span className="bap-chip">
            {VOICE[lang] || VOICE.en}: {c.voice}
          </span>
        </article>
      ))}
    </div>
  )
}

// A grid of round device screenshots: items = [[file, caption], ...]
export function Shots({ items }) {
  return (
    <div className="bap-shots">
      {items.map(([file, caption]) => (
        <figure key={file}>
          <img src={`/screens/${file}.png`} alt={caption} loading="lazy" width={300} height={300} />
          <figcaption>{caption}</figcaption>
        </figure>
      ))}
    </div>
  )
}
