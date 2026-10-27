#include "music_lyrics.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    char line[32];
    const char *lrc="[00:00.00]开场\n[00:01.20][00:02.40]第二句\n[00:05.00]第三句\n";
    music_lyrics_line(lrc,0,line,sizeof(line));assert(strcmp(line,"开场")==0);
    music_lyrics_line(lrc,1199,line,sizeof(line));assert(strcmp(line,"开场")==0);
    music_lyrics_line(lrc,1200,line,sizeof(line));assert(strcmp(line,"第二句")==0);
    music_lyrics_line(lrc,2400,line,sizeof(line));assert(strcmp(line,"第二句")==0);
    music_lyrics_line(lrc,8000,line,sizeof(line));assert(strcmp(line,"第三句")==0);
    music_lyrics_line("[ar:artist]\n[00:59.999]以后\n",59999,line,sizeof(line));assert(strcmp(line,"以后")==0);
    music_lyrics_line("[00:60.00]bad\n",90000,line,sizeof(line));assert(line[0]==0);
    music_lyrics_line("[00:01.00]这是一段很长的歌词文本\n",1000,line,8);
    assert(strcmp(line,"这是")==0);
    music_lyrics_line(NULL,1000,line,sizeof(line));assert(line[0]==0);
    return 0;
}
