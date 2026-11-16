#pragma once
// Vietnamese lunar calendar (âm lịch), UTC+7. Port of Hồ Ngọc Đức's public algorithm.
#include <cmath>
#include <string>

namespace vn_lunar {

struct LunarDate { int day, month, year; bool leap; long jd; };
struct SolarDate { int day, month, year; };

inline long JdFromDate(int dd, int mm, int yy) {
    int a = (14 - mm) / 12, y = yy + 4800 - a, m = mm + 12 * a - 3;
    return dd + (153 * m + 2) / 5 + 365L * y + y / 4 - y / 100 + y / 400 - 32045;
}

inline SolarDate JdToDate(long jd) {
    long a = jd + 32044, b = (4 * a + 3) / 146097, c = a - (b * 146097) / 4;
    long d = (4 * c + 3) / 1461, e = c - (1461 * d) / 4, m = (5 * e + 2) / 153;
    return {int(e - (153 * m + 2) / 5 + 1), int(m + 3 - 12 * (m / 10)), int(b * 100 + d - 4800 + m / 10)};
}

inline long NewMoonDay(long k, double tz = 7.0) {
    const double dr = M_PI / 180;
    double T = k / 1236.85, T2 = T * T, T3 = T2 * T;
    double Jd1 = 2415020.75933 + 29.53058868 * k + 0.0001178 * T2 - 0.000000155 * T3;
    Jd1 += 0.00033 * sin((166.56 + 132.87 * T - 0.009173 * T2) * dr);
    double M = 359.2242 + 29.10535608 * k - 0.0000333 * T2 - 0.00000347 * T3;
    double Mpr = 306.0253 + 385.81691806 * k + 0.0107306 * T2 + 0.00001236 * T3;
    double F = 21.2964 + 390.67050646 * k - 0.0016528 * T2 - 0.00000239 * T3;
    double C1 = (0.1734 - 0.000393 * T) * sin(M * dr) + 0.0021 * sin(2 * dr * M);
    C1 = C1 - 0.4068 * sin(Mpr * dr) + 0.0161 * sin(dr * 2 * Mpr);
    C1 = C1 - 0.0004 * sin(dr * 3 * Mpr);
    C1 = C1 + 0.0104 * sin(dr * 2 * F) - 0.0051 * sin(dr * (M + Mpr));
    C1 = C1 - 0.0074 * sin(dr * (M - Mpr)) + 0.0004 * sin(dr * (2 * F + M));
    C1 = C1 - 0.0004 * sin(dr * (2 * F - M)) - 0.0006 * sin(dr * (2 * F + Mpr));
    C1 = C1 + 0.0010 * sin(dr * (2 * F - Mpr)) + 0.0005 * sin(dr * (2 * Mpr + M));
    double deltat = T < -11 ? 0.001 + 0.000839 * T + 0.0002261 * T2 - 0.00000845 * T3 - 0.000000081 * T * T3
                            : -0.000278 + 0.000265 * T + 0.000262 * T2;
    return (long)floor(Jd1 + C1 - deltat + 0.5 + tz / 24);
}

inline int SunLongitude(long jdn, double tz = 7.0) {
    const double dr = M_PI / 180;
    double T = (jdn - 2451545.5 - tz / 24) / 36525, T2 = T * T;
    double M = 357.52910 + 35999.05030 * T - 0.0001559 * T2 - 0.00000048 * T * T2;
    double L0 = 280.46645 + 36000.76983 * T + 0.0003032 * T2;
    double DL = (1.914600 - 0.004817 * T - 0.000014 * T2) * sin(dr * M);
    DL += (0.019993 - 0.000101 * T) * sin(dr * 2 * M) + 0.000290 * sin(dr * 3 * M);
    double L = (L0 + DL) * dr;
    L -= M_PI * 2 * floor(L / (M_PI * 2));
    return (int)floor(L / M_PI * 6);
}

inline long LunarMonth11(int yy) {
    long off = JdFromDate(31, 12, yy) - 2415021;
    long k = (long)floor(off / 29.530588853);
    long nm = NewMoonDay(k);
    if (SunLongitude(nm) >= 9) nm = NewMoonDay(k - 1);
    return nm;
}

inline int LeapMonthOffset(long a11) {
    long k = (long)floor((a11 - 2415021.076998695) / 29.530588853 + 0.5);
    int last, i = 1, arc = SunLongitude(NewMoonDay(k + i));
    do { last = arc; i++; arc = SunLongitude(NewMoonDay(k + i)); } while (arc != last && i < 14);
    return i - 1;
}

inline LunarDate FromSolar(int dd, int mm, int yy) {
    long day = JdFromDate(dd, mm, yy);
    long k = (long)floor((day - 2415021.076998695) / 29.530588853);
    long month_start = NewMoonDay(k + 1);
    if (month_start > day) month_start = NewMoonDay(k);
    long a11 = LunarMonth11(yy), b11 = a11;
    int lunar_year;
    if (a11 >= month_start) { lunar_year = yy; a11 = LunarMonth11(yy - 1); }
    else { lunar_year = yy + 1; b11 = LunarMonth11(yy + 1); }
    int lunar_day = int(day - month_start + 1);
    int diff = int((month_start - a11) / 29);
    bool leap = false;
    int lunar_month = diff + 11;
    if (b11 - a11 > 365) {
        int leap_diff = LeapMonthOffset(a11);
        if (diff >= leap_diff) { lunar_month = diff + 10; if (diff == leap_diff) leap = true; }
    }
    if (lunar_month > 12) lunar_month -= 12;
    if (lunar_month >= 11 && diff < 4) lunar_year -= 1;
    return {lunar_day, lunar_month, lunar_year, leap, day};
}

// Returns day=0 when the requested leap month does not exist.
inline SolarDate ToSolar(int lunar_day, int lunar_month, int lunar_year, bool leap = false) {
    long a11, b11;
    if (lunar_month < 11) { a11 = LunarMonth11(lunar_year - 1); b11 = LunarMonth11(lunar_year); }
    else { a11 = LunarMonth11(lunar_year); b11 = LunarMonth11(lunar_year + 1); }
    long k = (long)floor(0.5 + (a11 - 2415021.076998695) / 29.530588853);
    int off = lunar_month - 11;
    if (off < 0) off += 12;
    if (b11 - a11 > 365) {
        int leap_off = LeapMonthOffset(a11);
        int leap_month = leap_off - 2;
        if (leap_month < 0) leap_month += 12;
        if (leap && lunar_month != leap_month) return {0, 0, 0};
        if (leap || off >= leap_off) off += 1;
    } else if (leap) {
        return {0, 0, 0};
    }
    return JdToDate(NewMoonDay(k + off) + lunar_day - 1);
}

inline const char* Can(int i) {
    static const char* k[] = {"Giáp", "Ất", "Bính", "Đinh", "Mậu", "Kỷ", "Canh", "Tân", "Nhâm", "Quý"};
    return k[((i % 10) + 10) % 10];
}
inline const char* Chi(int i) {
    static const char* k[] = {"Tý", "Sửu", "Dần", "Mão", "Thìn", "Tỵ", "Ngọ", "Mùi", "Thân", "Dậu", "Tuất", "Hợi"};
    return k[((i % 12) + 12) % 12];
}
inline std::string YearName(int lunar_year) { return std::string(Can(lunar_year + 6)) + " " + Chi(lunar_year + 8); }
inline std::string DayName(long jd) { return std::string(Can(int((jd + 9) % 10))) + " " + Chi(int((jd + 1) % 12)); }
inline std::string MonthName(int lunar_month, int lunar_year) {
    return std::string(Can(lunar_year * 12 + lunar_month + 3)) + " " + Chi(lunar_month + 1);
}

// Fixed-date lunar festivals; nullptr if none.
inline const char* Festival(const LunarDate& d) {
    if (d.leap) return nullptr;
    struct F { int d, m; const char* n; };
    static const F f[] = {{1, 1, "Mùng 1 Tết Nguyên Đán"}, {2, 1, "Mùng 2 Tết"}, {3, 1, "Mùng 3 Tết"},
        {15, 1, "Rằm tháng Giêng (Tết Nguyên Tiêu)"}, {10, 3, "Giỗ Tổ Hùng Vương"}, {15, 4, "Lễ Phật Đản"},
        {5, 5, "Tết Đoan Ngọ"}, {15, 7, "Rằm tháng Bảy (Vu Lan)"}, {15, 8, "Tết Trung Thu"},
        {23, 12, "Ông Công Ông Táo"}};
    for (auto& x : f) if (x.d == d.day && x.m == d.month) return x.n;
    return nullptr;
}

}  // namespace vn_lunar
