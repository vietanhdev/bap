// UI strings for the site chrome and the landing page. Doc pages live in content/<locale>/.
export const LOCALES = [
  { locale: 'en', name: 'English' },
  { locale: 'vi', name: 'Tiếng Việt' },
  { locale: 'ko', name: '한국어' },
  { locale: 'zh', name: '中文' }
]

export const SITE_URL = 'https://bap.nrl.ai'
export const REPO_URL = 'https://github.com/vietanhdev/bap'

const en = {
  title: 'Bắp',
  description:
    'Bắp is a cute voice assistant firmware for the Seeed SenseCAP Watcher. It talks straight to the Gemini Live API, with five characters, voice apps, music and on-device tools.',
  toc: 'On this page',
  backToTop: 'Back to top',
  editPage: 'Edit this page',
  feedback: 'Questions? Give us feedback',
  search: 'Search the docs…',
  footer: 'MIT licensed. Not affiliated with Seeed Studio or Google.',
  hero: {
    badge: 'Open-source firmware · ESP32-S3',
    title: 'A tiny friend that lives on your desk',
    lead: 'Bắp turns the round Seeed SenseCAP Watcher into a cute voice assistant. It streams your voice straight to Google Gemini Live and answers in real time, with its own face, voice and personality.',
    start: 'Get started',
    github: 'GitHub',
    noRelay: 'No relay server: the device talks to Gemini directly over TLS.'
  },
  stats: [
    ['5', 'characters'],
    ['11', 'voice apps'],
    ['29', 'device tools'],
    ['4', 'languages']
  ],
  featuresTitle: 'Everything on one round screen',
  features: [
    ['🎙️', 'Real-time conversation', 'Full-duplex audio with Gemini Live. Interrupt any time; subtitles follow the voice.'],
    ['🌽', 'Five characters', 'Bông, Bắp, Bơ, Mochi and Tí, each with a voice, a personality and a face drawn natively with 23 expressions.'],
    ['📻', 'Music and radio', 'Zing MP3 with synced lyrics, VOV and Hanoi FM radio, and offline music from the SD card.'],
    ['🧰', 'Useful tools', 'Lunar calendar, weather, news, gold prices, exchange rates, stocks, football, timers, alarms, notes and memory.'],
    ['👀', 'Sees you', 'The camera’s on-chip AI notices when someone is there. No image leaves the device.'],
    ['🌙', 'Gentle at night', 'Night mode dims the screen and caps the volume; quiet hours ignore the wake word.'],
    ['⚙️', 'Web settings and OTA', 'A settings page on bap-xxxx.local, optional password, firmware updates over Wi-Fi in about 30 s.'],
    ['🌏', 'Four languages', 'Vietnamese, English, Korean and Chinese, for the voice and the on-screen UI.']
  ],
  galleryTitle: 'Real screenshots',
  galleryLead: 'Captured from the device and its settings page.',
  gallery: {
    listening: 'Listening',
    talking: 'Talking with subtitles',
    music: 'Music with synced lyrics',
    radio: 'Radio',
    menu: 'Round menu',
    settings: 'Settings menu',
    korean: 'Korean subtitles',
    chinese: 'Chinese subtitles'
  },
  webTitle: 'Settings in your browser',
  web: ['Chat and quick actions', 'Characters and personalization', 'System settings', 'Device health'],
  howTitle: 'How it works',
  howLead: 'One device, one TLS connection, no server in between.',
  ctaTitle: 'Make your Watcher talk',
  ctaLead: 'Build the firmware, flash it once, then update over Wi-Fi.'
}

const vi = {
  ...en,
  description:
    'Bắp là firmware trợ lý giọng nói dễ thương cho Seeed SenseCAP Watcher. Thiết bị nói chuyện trực tiếp với Gemini Live, có 5 nhân vật, ứng dụng giọng nói, nhạc và công cụ ngay trên máy.',
  toc: 'Trong trang này',
  backToTop: 'Lên đầu trang',
  editPage: 'Sửa trang này',
  feedback: 'Có câu hỏi? Góp ý cho chúng mình',
  search: 'Tìm trong tài liệu…',
  footer: 'Giấy phép MIT. Không liên kết với Seeed Studio hay Google.',
  hero: {
    badge: 'Firmware mã nguồn mở · ESP32-S3',
    title: 'Người bạn nhỏ trên bàn làm việc',
    lead: 'Bắp biến chiếc Seeed SenseCAP Watcher tròn xinh thành trợ lý giọng nói dễ thương. Giọng nói của bạn đi thẳng tới Google Gemini Live và được trả lời ngay, với gương mặt, giọng nói và tính cách riêng.',
    start: 'Bắt đầu',
    github: 'GitHub',
    noRelay: 'Không cần máy chủ trung gian: thiết bị nói chuyện trực tiếp với Gemini qua TLS.'
  },
  stats: [
    ['5', 'nhân vật'],
    ['11', 'ứng dụng giọng nói'],
    ['29', 'công cụ trên máy'],
    ['4', 'ngôn ngữ']
  ],
  featuresTitle: 'Mọi thứ trên một màn hình tròn',
  features: [
    ['🎙️', 'Trò chuyện thời gian thực', 'Âm thanh hai chiều với Gemini Live. Ngắt lời bất cứ lúc nào, phụ đề chạy theo giọng nói.'],
    ['🌽', 'Năm nhân vật', 'Bông, Bắp, Bơ, Mochi và Tí, mỗi bạn một giọng, một tính cách và gương mặt vẽ trực tiếp với 23 biểu cảm.'],
    ['📻', 'Nhạc và radio', 'Zing MP3 kèm lời chạy theo nhạc, radio VOV và FM Hà Nội, nhạc trong thẻ SD không cần mạng.'],
    ['🧰', 'Công cụ hữu ích', 'Lịch âm, thời tiết, tin tức, giá vàng, tỷ giá, chứng khoán, bóng đá, hẹn giờ, báo thức, ghi chú và ghi nhớ.'],
    ['👀', 'Nhận ra bạn', 'Chip AI trong camera nhận biết có người ngay trên máy. Không ảnh nào rời khỏi thiết bị.'],
    ['🌙', 'Dịu dàng ban đêm', 'Chế độ đêm làm dịu màn hình và giới hạn âm lượng; giờ yên tĩnh tắt từ đánh thức.'],
    ['⚙️', 'Cài đặt web và OTA', 'Trang cài đặt tại bap-xxxx.local, mật khẩu tùy chọn, cập nhật firmware qua Wi-Fi trong khoảng 30 giây.'],
    ['🌏', 'Bốn ngôn ngữ', 'Tiếng Việt, Anh, Hàn và Trung cho cả giọng nói lẫn giao diện trên màn hình.']
  ],
  galleryTitle: 'Ảnh chụp thật',
  galleryLead: 'Chụp từ màn hình thiết bị và trang cài đặt.',
  gallery: {
    listening: 'Đang lắng nghe',
    talking: 'Đang nói kèm phụ đề',
    music: 'Nhạc kèm lời bài hát',
    radio: 'Radio',
    menu: 'Menu tròn',
    settings: 'Menu thiết lập',
    korean: 'Phụ đề tiếng Hàn',
    chinese: 'Phụ đề tiếng Trung'
  },
  webTitle: 'Cài đặt ngay trên trình duyệt',
  web: ['Trò chuyện và thao tác nhanh', 'Nhân vật và cá nhân hóa', 'Thiết lập hệ thống', 'Tình trạng máy'],
  howTitle: 'Cách hoạt động',
  howLead: 'Một thiết bị, một kết nối TLS, không có máy chủ ở giữa.',
  ctaTitle: 'Cho chiếc Watcher của bạn biết nói',
  ctaLead: 'Build firmware, flash một lần, những lần sau cập nhật qua Wi-Fi.'
}

const ko = {
  ...en,
  description:
    'Bắp은 Seeed SenseCAP Watcher를 위한 귀여운 음성 비서 펌웨어입니다. 기기가 Gemini Live API와 직접 대화하며, 다섯 캐릭터와 음성 앱, 음악, 기기 내 도구를 제공합니다.',
  toc: '이 페이지의 내용',
  backToTop: '맨 위로',
  editPage: '이 페이지 수정',
  feedback: '질문이 있나요? 의견을 보내 주세요',
  search: '문서 검색…',
  footer: 'MIT 라이선스. Seeed Studio 및 Google과 관련이 없습니다.',
  hero: {
    badge: '오픈 소스 펌웨어 · ESP32-S3',
    title: '책상 위의 작은 친구',
    lead: 'Bắp은 동그란 Seeed SenseCAP Watcher를 귀여운 음성 비서로 바꿔 줍니다. 목소리가 Google Gemini Live로 바로 전달되어 실시간으로 답하며, 저마다의 얼굴과 목소리, 성격을 가지고 있습니다.',
    start: '시작하기',
    github: 'GitHub',
    noRelay: '중계 서버 없음: 기기가 TLS로 Gemini와 직접 통신합니다.'
  },
  stats: [
    ['5', '캐릭터'],
    ['11', '음성 앱'],
    ['29', '기기 도구'],
    ['4', '언어']
  ],
  featuresTitle: '동그란 화면 하나에 모든 것을',
  features: [
    ['🎙️', '실시간 대화', 'Gemini Live와 양방향 오디오로 대화합니다. 언제든 말을 끊을 수 있고 자막이 음성을 따라갑니다.'],
    ['🌽', '다섯 캐릭터', 'Bông, Bắp, Bơ, Mochi, Tí. 각자 목소리와 성격, 23가지 표정을 가진 얼굴이 기기에서 직접 그려집니다.'],
    ['📻', '음악과 라디오', '가사가 동기화되는 Zing MP3, VOV와 하노이 FM 라디오, SD 카드의 오프라인 음악.'],
    ['🧰', '유용한 도구', '음력, 날씨, 뉴스, 금 시세, 환율, 주식, 축구, 타이머, 알람, 메모, 기억.'],
    ['👀', '사람을 알아봐요', '카메라의 온칩 AI가 사람이 있는지 알아챕니다. 이미지는 기기 밖으로 나가지 않습니다.'],
    ['🌙', '밤에는 조용히', '야간 모드는 화면을 어둡게 하고 음량을 제한하며, 방해 금지 시간에는 호출어를 무시합니다.'],
    ['⚙️', '웹 설정과 OTA', 'bap-xxxx.local 설정 페이지, 선택적 비밀번호, Wi-Fi로 약 30초 만에 펌웨어 업데이트.'],
    ['🌏', '네 가지 언어', '베트남어, 영어, 한국어, 중국어로 음성과 화면 UI를 모두 지원합니다.']
  ],
  galleryTitle: '실제 화면',
  galleryLead: '기기 화면과 설정 페이지에서 캡처했습니다.',
  gallery: {
    listening: '듣는 중',
    talking: '자막과 함께 말하기',
    music: '가사가 동기화되는 음악',
    radio: '라디오',
    menu: '원형 메뉴',
    settings: '설정 메뉴',
    korean: '한국어 자막',
    chinese: '중국어 자막'
  },
  webTitle: '브라우저에서 설정',
  web: ['대화와 빠른 실행', '캐릭터와 개인화', '시스템 설정', '기기 상태'],
  howTitle: '작동 방식',
  howLead: '기기 하나, TLS 연결 하나, 중간 서버 없음.',
  ctaTitle: '당신의 Watcher가 말하게 하세요',
  ctaLead: '펌웨어를 빌드해 한 번 플래시하면, 이후에는 Wi-Fi로 업데이트합니다.'
}

const zh = {
  ...en,
  description:
    'Bắp 是为 Seeed SenseCAP Watcher 打造的可爱语音助手固件。设备直接与 Gemini Live API 对话，提供五个角色、语音应用、音乐和设备端工具。',
  toc: '本页内容',
  backToTop: '回到顶部',
  editPage: '编辑此页',
  feedback: '有问题？欢迎反馈',
  search: '搜索文档…',
  footer: 'MIT 许可。与 Seeed Studio 和 Google 无关。',
  hero: {
    badge: '开源固件 · ESP32-S3',
    title: '住在桌上的小伙伴',
    lead: 'Bắp 把圆形的 Seeed SenseCAP Watcher 变成可爱的语音助手。你的声音直接发送到 Google Gemini Live 并实时回答，每个角色都有自己的表情、声音和性格。',
    start: '开始使用',
    github: 'GitHub',
    noRelay: '无中转服务器：设备通过 TLS 直接与 Gemini 通信。'
  },
  stats: [
    ['5', '个角色'],
    ['11', '个语音应用'],
    ['29', '个设备工具'],
    ['4', '种语言']
  ],
  featuresTitle: '一块圆屏，应有尽有',
  features: [
    ['🎙️', '实时对话', '与 Gemini Live 全双工语音对话。随时可以打断，字幕跟随语音。'],
    ['🌽', '五个角色', 'Bông、Bắp、Bơ、Mochi 和 Tí，各有声音、性格，以及在设备上原生绘制的 23 种表情。'],
    ['📻', '音乐与电台', 'Zing MP3 同步歌词，VOV 与河内 FM 电台，以及 SD 卡离线音乐。'],
    ['🧰', '实用工具', '农历、天气、新闻、金价、汇率、股市、足球、计时器、闹钟、笔记和记忆。'],
    ['👀', '能看见你', '摄像头的片上 AI 能识别有人在场，图像不会离开设备。'],
    ['🌙', '夜间更安静', '夜间模式调暗屏幕并限制音量；免打扰时段忽略唤醒词。'],
    ['⚙️', '网页设置与 OTA', '在 bap-xxxx.local 设置，可选密码，通过 Wi-Fi 约 30 秒完成固件更新。'],
    ['🌏', '四种语言', '越南语、英语、韩语和中文，语音和屏幕界面都支持。']
  ],
  galleryTitle: '真实截图',
  galleryLead: '截取自设备屏幕和设置页面。',
  gallery: {
    listening: '聆听中',
    talking: '说话并显示字幕',
    music: '同步歌词的音乐',
    radio: '电台',
    menu: '圆形菜单',
    settings: '设置菜单',
    korean: '韩语字幕',
    chinese: '中文字幕'
  },
  webTitle: '在浏览器中设置',
  web: ['聊天与快捷操作', '角色与个性化', '系统设置', '设备状态'],
  howTitle: '工作原理',
  howLead: '一台设备，一条 TLS 连接，中间没有服务器。',
  ctaTitle: '让你的 Watcher 开口说话',
  ctaLead: '编译固件，刷写一次，之后通过 Wi-Fi 更新。'
}

export const DICT = { en, vi, ko, zh }
export const t = lang => DICT[lang] || en
