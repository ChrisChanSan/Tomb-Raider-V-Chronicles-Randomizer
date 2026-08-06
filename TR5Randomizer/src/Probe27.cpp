#include "core/Tr5Level.h"
#include <cstdio>
int main(int argc,char**argv){ Tr5Level l; std::string e;
 std::wstring p; for(char*q=argv[1];*q;++q) p.push_back((wchar_t)(unsigned char)*q);
 if(!l.load(p,e)){std::printf("%s\n",e.c_str());return 1;}
 Tr5Item it{}; l.getItem(27,it);
 std::printf("Unser Leser    : Item 27  Raum %d  Pos (%d, %d, %d)  flags 0x%04X\n",
   it.roomNumber,it.x,it.y,it.z,(unsigned short)it.flags); return 0; }
