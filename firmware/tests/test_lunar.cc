#include "../src/assistant/vn_lunar.h"
#include <cstdio>
using namespace vn_lunar;
int fails = 0;
void chk(int d,int m,int y,int ld,int lm,int ly,bool leap){ auto l=FromSolar(d,m,y); bool ok=l.day==ld&&l.month==lm&&l.year==ly&&l.leap==leap; auto s=ToSolar(ld,lm,ly,leap); ok=ok&&s.day==d&&s.month==m&&s.year==y; printf("%s %02d/%02d/%d -> %d/%d%s/%d %s, ngay %s\n", ok?"ok  ":"FAIL",d,m,y,l.day,l.month,l.leap?"n":"",l.year,YearName(l.year).c_str(),DayName(l.jd).c_str()); if(!ok) fails++; }
int main(){
  chk(10,2,2024,1,1,2024,false);   // Tet Giap Thin
  chk(29,1,2025,1,1,2025,false);   // Tet At Ty
  chk(17,2,2026,1,1,2026,false);   // Tet Binh Ngo
  chk(6,2,2027,1,1,2027,false);    // Tet Dinh Mui
  chk(25,7,2025,1,6,2025,true);    // leap 6th month 2025 starts 25 Jul
  chk(6,10,2025,15,8,2025,false);  // Trung thu 2025
  chk(1,1,2000,25,11,1999,false);
  return fails;
}
