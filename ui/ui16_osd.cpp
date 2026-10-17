#ifdef UI16_SYSTEM_MENU
#include "ui16_osd.h"
#include <algorithm>
#include <string>

namespace { Ui16OsdCanvas canvas; bool capturing=false; }
void ui16_osd_capture_begin(){canvas.cells.fill(' ');canvas.x=canvas.y=0;capturing=true;}
void ui16_osd_capture_end(){capturing=false;}
bool ui16_osd_capture_active(){return capturing;}
bool ui16_osd_capture_clear(){if(!capturing)return false;canvas.cells.fill(' ');canvas.x=canvas.y=0;return true;}
bool ui16_osd_capture_cursor(int x,int y){if(!capturing)return false;canvas.x=x;canvas.y=y;return true;}
bool ui16_osd_capture_text(const char *s){
    if(!capturing)return false;
    for(;*s;++s){
        if(*s=='\n'){canvas.x=0;++canvas.y;continue;}
        if(*s=='\r')continue;
        if(canvas.x>=0&&canvas.x<32&&canvas.y>=0&&canvas.y<Ui16OsdCanvas::Rows)
            canvas.cells[canvas.y*32+canvas.x]=*s;
        ++canvas.x;
    }
    return true;
}
const Ui16OsdCanvas &ui16_osd_canvas(){return canvas;}
const char *ui16_osd_position_name(Ui16OsdPosition p){
    switch(p){
    case Ui16OsdPosition::Center:return "CENTER";
    case Ui16OsdPosition::TopLeft:return "TOP LEFT";
    case Ui16OsdPosition::TopRight:return "TOP RIGHT";
    case Ui16OsdPosition::BottomLeft:return "BOTTOM LEFT";
    case Ui16OsdPosition::BottomRight:return "BOTTOM RIGHT";
    }
    return "TOP RIGHT";
}
Ui16OsdFrame ui16_osd_layout(const Ui16OsdCanvas &c,const std::vector<int> &options,
    int selected,int &top,int &width_floor,Ui16OsdPosition position)
{
    int first=Ui16OsdCanvas::Rows-1,last=0,width=0;
    for(int y=0;y<Ui16OsdCanvas::Rows;++y)for(int x=0;x<32;++x)
        if(c.cells[y*32+x]!=' '&&c.cells[y*32+x]!=0){first=std::min(first,y);last=std::max(last,y);width=std::max(width,x+1);}
    for(int row:options)if(row>=0&&row<Ui16OsdCanvas::Rows){first=std::min(first,row);last=std::max(last,row);}
    if(first>last)first=last=0;
    const int lines=std::min(21,last-first+1);
    top=std::max(first,std::min(top,last-lines+1));
    if(selected>=0&&selected<static_cast<int>(options.size())){
        const int row=options[selected];
        if(row>=first&&row<=last){if(row<top)top=row;else if(row>=top+lines)top=row-lines+1;}
    }
    width_floor=std::min(27,std::max({width_floor,width,16}));
    Ui16OsdFrame f;f.cells.fill(' ');f.width=width_floor+3;f.height=lines+5;
    const auto p=position;
    f.x=(p==Ui16OsdPosition::Center)?(32-f.width)/2:
        (p==Ui16OsdPosition::TopRight||p==Ui16OsdPosition::BottomRight)?31-f.width:1;
    f.y=(p==Ui16OsdPosition::Center)?(28-f.height)/2:
        (p==Ui16OsdPosition::BottomLeft||p==Ui16OsdPosition::BottomRight)?27-f.height:1;
    auto put=[&](int x,int y,char v){if(x>=0&&y>=0&&x<static_cast<int>(f.width)&&y<static_cast<int>(f.height))f.cells[(f.y+y)*32+f.x+x]=v;};
    auto text=[&](int x,int y,const std::string &s){for(char v:s){if(x>=static_cast<int>(f.width)-1)break;put(x++,y,v);}};
    for(unsigned x=0;x<f.width;++x){put(x,0,'-');put(x,f.height-1,'-');}
    for(unsigned y=0;y<f.height;++y){put(0,y,'|');put(f.width-1,y,'|');}
    put(0,0,'+');put(f.width-1,0,'+');put(0,f.height-1,'+');put(f.width-1,f.height-1,'+');
    text(2,1,"MENU");
    for(int i=0;i<lines;++i){
        for(int x=0;x<width_floor;++x)put(x+2,i+2,c.cells[(top+i)*32+x]);
        if(selected>=0&&selected<static_cast<int>(options.size())&&options[selected]==top+i)put(1,i+2,'>');
    }
    text(2,lines+2,std::string("POS ")+ui16_osd_position_name(position));
    if(selected==static_cast<int>(options.size()))put(1,lines+2,'>');
    text(2,lines+3,"A/B OK X/Y BACK");
    return f;
}
#endif
