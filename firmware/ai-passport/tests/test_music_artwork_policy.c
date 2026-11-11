#include "music_artwork_policy.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    char url[320];
    assert(music_artwork_request_url("http://p1.music.126.net/album.jpg",url,sizeof(url)));
    assert(!strcmp(url,"https://p1.music.126.net/album.jpg?param=60y60"));
    assert(music_artwork_request_url("https://p1.music.126.net/a?x=1",url,sizeof(url)));
    assert(strstr(url,"&param=60y60"));
    const char *qq="https://y.gtimg.cn/music/photo_new/T002R90x90M000000MkMni19ClKG.jpg";
    assert(music_artwork_request_url(qq,url,sizeof(url)) && !strcmp(qq,url));
    const char *invalid[]={"https://y.gtimg.cn.evil.test/a", "https://evil.test/p1.music.126.net/a",
        "https://p1.music.126.net@evil.test/a", "https://y.gtimg.cn/other.jpg",
        "https://y.gtimg.cn/music/photo_new/T002R90x90M000../a.jpg",
        "https://y.gtimg.cn/music/photo_new/T002R90x90M000.jpg",
        "https://y.gtimg.cn/music/photo_new/T002R90x90M000x.jpg?redirect=1",
        "https://p1.music.126.net/a\r\nX: bad", "https://p1.music.126.net:443/a"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        assert(!music_artwork_request_url(invalid[i],url,sizeof(url)) && !url[0]);
    }
    assert(!music_artwork_request_url(qq,url,8) && !url[0]);
    return 0;
}
