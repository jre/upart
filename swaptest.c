#if 0 /*

import random, struct, sys

rand = random.Random("\xbd-\x03\xd1'-\xc9\x1e\x954C3\xb5\x8d\xfc\xeb")

def esc(bin):
    return ''.join(['\\x%02x' % ord(ii) for ii in bin])

def mkbufargs(name, size):
    return ', '.join(['%s[%d]' % (name, ii) for ii in range(size)])

magic = ''.join(reversed(list('cigam gnikcuf')))
data  = file(sys.argv[0]).read()

print '''%s %s
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "util.h"
''' % (data[:data.index(magic)+len(magic)], '*' + '/')

for bits, fmt in ((16, 'H'), (32, 'L'), (64, 'Q')):
    print '''int
dotest%d(void)
{
    struct { char *big; char *lit; uint%d_t native; } tests[] = {''' % \
    (bits, bits)
    for dummy in xrange(128):
        num = rand.getrandbits(bits)
        little = struct.pack('<' + fmt, num)
        big = struct.pack('>' + fmt, num)
        print '        {"%s", "%s", UINT%d_C(0x%0*x)},' % \
              (esc(big), esc(little), bits, bits / 4, num)
    print '''    };
    uint%(bits)d_t natl, natb, bign, litn;
    uint8_t bufl[%(bytes)d], bufb[%(bytes)d];
    int ii, fail;

    fail = 0;
    for(ii = 0; sizeof(tests) / sizeof(tests[0]) > ii; ii++)
    {
        assert(sizeof bign == %(bytes)d);
        natl = UP_GETBUF%(bits)dLE(tests[ii].lit);
        natb = UP_GETBUF%(bits)dBE(tests[ii].big);
        UP_SETBUF%(bits)dLE(bufl, tests[ii].native);
        UP_SETBUF%(bits)dBE(bufb, tests[ii].native);
        memcpy(&bign, tests[ii].big, %(bytes)d);
        memcpy(&litn, tests[ii].lit, %(bytes)d);

        if(natl == tests[ii].native &&
           natb == tests[ii].native &&
           !memcmp(bufl, tests[ii].lit, %(bytes)d) &&
           !memcmp(bufb, tests[ii].big, %(bytes)d) &&
           litn == UP_HTOLE%(bits)d(tests[ii].native) &&
           bign == UP_HTOBE%(bits)d(tests[ii].native) &&
           tests[ii].native == UP_LETOH%(bits)d(litn) &&
           tests[ii].native == UP_BETOH%(bits)d(bign))
            continue;

        fail = 1;
        printf("%(bits)d/%%d\\t", ii);
        if(natl != tests[ii].native)
            printf(" getle:(%(intfmt)s!=%(intfmt)s)", natl, tests[ii].native);
        if(natb != tests[ii].native)
            printf(" getbe:(%(intfmt)s!=%(intfmt)s)", natb, tests[ii].native);
        if(memcmp(bufl, tests[ii].lit, %(bytes)d))
            printf(" setle:(%(buffmt)s!=%(buffmt)s)",
                   %(blarg)s,
                   %(tlarg)s);
        if(memcmp(bufb, tests[ii].big, %(bytes)d))
            printf(" setle:(%(buffmt)s!=%(buffmt)s)",
                   %(bbarg)s,
                   %(tbarg)s);
        if(litn != UP_HTOLE%(bits)d(tests[ii].native))
            printf(" htole:(%(intfmt)s!=%(intfmt)s)",
                   litn, UP_HTOLE%(bits)d(tests[ii].native));
        if(bign != UP_HTOBE%(bits)d(tests[ii].native))
            printf(" htobe:(%(intfmt)s!=%(intfmt)s)",
                   bign, UP_HTOBE%(bits)d(tests[ii].native));
        if(tests[ii].native != UP_LETOH%(bits)d(litn))
            printf(" letoh:(%(intfmt)s!=%(intfmt)s)",
                   tests[ii].native, UP_LETOH%(bits)d(litn));
        if(tests[ii].native != UP_BETOH%(bits)d(bign))
            printf(" betoh:(%(intfmt)s!=%(intfmt)s)",
                   tests[ii].native, UP_BETOH%(bits)d(bign));
        putc('\\n', stdout);
    }

    return fail;
}
''' % {'bits':   bits,
       'bytes':  bits / 8,
       'intfmt': '%%0%d"PRIx%d"' % (bits / 4, bits),
       'buffmt': '%02x' * (bits / 4),
       'blarg': mkbufargs('bufl', bits / 4),
       'bbarg': mkbufargs('bufb', bits / 4),
       'tlarg': mkbufargs('tests[ii].lit', bits / 4),
       'tbarg': mkbufargs('tests[ii].big', bits / 4)}

print '''int
main()
{
    int f16, f32, f64;

    if(0 > up_getendian())
        return 1;

    f16 = dotest16();
    f32 = dotest32();
    f64 = dotest64();

    return f16 || f32 || f64;
}
'''
print "// '''"
''' fucking magic */
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "util.h"

int
dotest16(void)
{
    struct { char *big; char *lit; uint16_t native; } tests[] = {
        {"\x9c\xef", "\xef\x9c", UINT16_C(0x9cef)},
        {"\xef\x68", "\x68\xef", UINT16_C(0xef68)},
        {"\x12\x06", "\x06\x12", UINT16_C(0x1206)},
        {"\x06\xc9", "\xc9\x06", UINT16_C(0x06c9)},
        {"\xb6\xbc", "\xbc\xb6", UINT16_C(0xb6bc)},
        {"\x4c\xe7", "\xe7\x4c", UINT16_C(0x4ce7)},
        {"\x2b\x16", "\x16\x2b", UINT16_C(0x2b16)},
        {"\x37\x15", "\x15\x37", UINT16_C(0x3715)},
        {"\xee\xea", "\xea\xee", UINT16_C(0xeeea)},
        {"\xf3\xa5", "\xa5\xf3", UINT16_C(0xf3a5)},
        {"\x2c\x28", "\x28\x2c", UINT16_C(0x2c28)},
        {"\xb8\xaf", "\xaf\xb8", UINT16_C(0xb8af)},
        {"\xa9\x9d", "\x9d\xa9", UINT16_C(0xa99d)},
        {"\x57\x4b", "\x4b\x57", UINT16_C(0x574b)},
        {"\xf4\xc0", "\xc0\xf4", UINT16_C(0xf4c0)},
        {"\x8f\x2e", "\x2e\x8f", UINT16_C(0x8f2e)},
        {"\x9e\x52", "\x52\x9e", UINT16_C(0x9e52)},
        {"\xef\xab", "\xab\xef", UINT16_C(0xefab)},
        {"\x59\x2c", "\x2c\x59", UINT16_C(0x592c)},
        {"\x0a\x33", "\x33\x0a", UINT16_C(0x0a33)},
        {"\x7d\x46", "\x46\x7d", UINT16_C(0x7d46)},
        {"\x9a\x9a", "\x9a\x9a", UINT16_C(0x9a9a)},
        {"\x20\x31", "\x31\x20", UINT16_C(0x2031)},
        {"\xf1\xba", "\xba\xf1", UINT16_C(0xf1ba)},
        {"\x28\x85", "\x85\x28", UINT16_C(0x2885)},
        {"\x92\x6f", "\x6f\x92", UINT16_C(0x926f)},
        {"\x73\x83", "\x83\x73", UINT16_C(0x7383)},
        {"\xc0\x93", "\x93\xc0", UINT16_C(0xc093)},
        {"\x3d\x7c", "\x7c\x3d", UINT16_C(0x3d7c)},
        {"\x45\xbf", "\xbf\x45", UINT16_C(0x45bf)},
        {"\xb6\x9a", "\x9a\xb6", UINT16_C(0xb69a)},
        {"\x08\xfa", "\xfa\x08", UINT16_C(0x08fa)},
        {"\xb7\x09", "\x09\xb7", UINT16_C(0xb709)},
        {"\x2a\x58", "\x58\x2a", UINT16_C(0x2a58)},
        {"\x1f\xf9", "\xf9\x1f", UINT16_C(0x1ff9)},
        {"\xc9\xf3", "\xf3\xc9", UINT16_C(0xc9f3)},
        {"\xa6\x18", "\x18\xa6", UINT16_C(0xa618)},
        {"\x45\x46", "\x46\x45", UINT16_C(0x4546)},
        {"\x69\xde", "\xde\x69", UINT16_C(0x69de)},
        {"\x64\x4a", "\x4a\x64", UINT16_C(0x644a)},
        {"\x3f\x2b", "\x2b\x3f", UINT16_C(0x3f2b)},
        {"\x04\x0d", "\x0d\x04", UINT16_C(0x040d)},
        {"\xf4\x1c", "\x1c\xf4", UINT16_C(0xf41c)},
        {"\x18\x7c", "\x7c\x18", UINT16_C(0x187c)},
        {"\x6b\xb3", "\xb3\x6b", UINT16_C(0x6bb3)},
        {"\xbd\x3e", "\x3e\xbd", UINT16_C(0xbd3e)},
        {"\x30\x8f", "\x8f\x30", UINT16_C(0x308f)},
        {"\x2c\xa3", "\xa3\x2c", UINT16_C(0x2ca3)},
        {"\x69\x41", "\x41\x69", UINT16_C(0x6941)},
        {"\x71\x17", "\x17\x71", UINT16_C(0x7117)},
        {"\xc0\x12", "\x12\xc0", UINT16_C(0xc012)},
        {"\xe1\xed", "\xed\xe1", UINT16_C(0xe1ed)},
        {"\x3b\xf6", "\xf6\x3b", UINT16_C(0x3bf6)},
        {"\x67\x41", "\x41\x67", UINT16_C(0x6741)},
        {"\x6e\x64", "\x64\x6e", UINT16_C(0x6e64)},
        {"\x0c\x39", "\x39\x0c", UINT16_C(0x0c39)},
        {"\xb8\xcf", "\xcf\xb8", UINT16_C(0xb8cf)},
        {"\x8c\x22", "\x22\x8c", UINT16_C(0x8c22)},
        {"\xbd\x7a", "\x7a\xbd", UINT16_C(0xbd7a)},
        {"\xe4\x1b", "\x1b\xe4", UINT16_C(0xe41b)},
        {"\x58\xd7", "\xd7\x58", UINT16_C(0x58d7)},
        {"\xac\x09", "\x09\xac", UINT16_C(0xac09)},
        {"\x82\x09", "\x09\x82", UINT16_C(0x8209)},
        {"\x8d\xbe", "\xbe\x8d", UINT16_C(0x8dbe)},
        {"\x56\x4a", "\x4a\x56", UINT16_C(0x564a)},
        {"\x35\xea", "\xea\x35", UINT16_C(0x35ea)},
        {"\x0e\xb1", "\xb1\x0e", UINT16_C(0x0eb1)},
        {"\x5d\x99", "\x99\x5d", UINT16_C(0x5d99)},
        {"\xe7\x9e", "\x9e\xe7", UINT16_C(0xe79e)},
        {"\xe2\x5a", "\x5a\xe2", UINT16_C(0xe25a)},
        {"\xdf\x39", "\x39\xdf", UINT16_C(0xdf39)},
        {"\xc3\x2d", "\x2d\xc3", UINT16_C(0xc32d)},
        {"\xbb\x51", "\x51\xbb", UINT16_C(0xbb51)},
        {"\xe5\xf1", "\xf1\xe5", UINT16_C(0xe5f1)},
        {"\xf1\x26", "\x26\xf1", UINT16_C(0xf126)},
        {"\x16\x89", "\x89\x16", UINT16_C(0x1689)},
        {"\x04\xdd", "\xdd\x04", UINT16_C(0x04dd)},
        {"\x7c\xd3", "\xd3\x7c", UINT16_C(0x7cd3)},
        {"\x26\x70", "\x70\x26", UINT16_C(0x2670)},
        {"\x20\x1c", "\x1c\x20", UINT16_C(0x201c)},
        {"\x4c\x4b", "\x4b\x4c", UINT16_C(0x4c4b)},
        {"\xd2\x21", "\x21\xd2", UINT16_C(0xd221)},
        {"\x09\x2a", "\x2a\x09", UINT16_C(0x092a)},
        {"\xa7\x68", "\x68\xa7", UINT16_C(0xa768)},
        {"\x6e\x11", "\x11\x6e", UINT16_C(0x6e11)},
        {"\x47\x41", "\x41\x47", UINT16_C(0x4741)},
        {"\xea\x0d", "\x0d\xea", UINT16_C(0xea0d)},
        {"\x35\xa2", "\xa2\x35", UINT16_C(0x35a2)},
        {"\x3b\x49", "\x49\x3b", UINT16_C(0x3b49)},
        {"\xb9\x40", "\x40\xb9", UINT16_C(0xb940)},
        {"\x52\x3e", "\x3e\x52", UINT16_C(0x523e)},
        {"\xb8\x19", "\x19\xb8", UINT16_C(0xb819)},
        {"\x59\x3f", "\x3f\x59", UINT16_C(0x593f)},
        {"\xec\x0d", "\x0d\xec", UINT16_C(0xec0d)},
        {"\x02\x7f", "\x7f\x02", UINT16_C(0x027f)},
        {"\x31\xda", "\xda\x31", UINT16_C(0x31da)},
        {"\x27\xe9", "\xe9\x27", UINT16_C(0x27e9)},
        {"\x9a\x4c", "\x4c\x9a", UINT16_C(0x9a4c)},
        {"\x75\xf3", "\xf3\x75", UINT16_C(0x75f3)},
        {"\xab\x3f", "\x3f\xab", UINT16_C(0xab3f)},
        {"\xf7\xa1", "\xa1\xf7", UINT16_C(0xf7a1)},
        {"\xed\x5a", "\x5a\xed", UINT16_C(0xed5a)},
        {"\x7e\xd8", "\xd8\x7e", UINT16_C(0x7ed8)},
        {"\x4b\x9d", "\x9d\x4b", UINT16_C(0x4b9d)},
        {"\x6e\x02", "\x02\x6e", UINT16_C(0x6e02)},
        {"\xe2\x01", "\x01\xe2", UINT16_C(0xe201)},
        {"\xaa\xdb", "\xdb\xaa", UINT16_C(0xaadb)},
        {"\x1c\xcd", "\xcd\x1c", UINT16_C(0x1ccd)},
        {"\xde\x5a", "\x5a\xde", UINT16_C(0xde5a)},
        {"\xe1\xe7", "\xe7\xe1", UINT16_C(0xe1e7)},
        {"\xe9\x29", "\x29\xe9", UINT16_C(0xe929)},
        {"\x83\xdf", "\xdf\x83", UINT16_C(0x83df)},
        {"\xd7\xdc", "\xdc\xd7", UINT16_C(0xd7dc)},
        {"\xd4\xba", "\xba\xd4", UINT16_C(0xd4ba)},
        {"\xf8\x83", "\x83\xf8", UINT16_C(0xf883)},
        {"\x2d\x35", "\x35\x2d", UINT16_C(0x2d35)},
        {"\xa5\xe9", "\xe9\xa5", UINT16_C(0xa5e9)},
        {"\x23\x78", "\x78\x23", UINT16_C(0x2378)},
        {"\xb6\x85", "\x85\xb6", UINT16_C(0xb685)},
        {"\x43\xe3", "\xe3\x43", UINT16_C(0x43e3)},
        {"\xcb\xaa", "\xaa\xcb", UINT16_C(0xcbaa)},
        {"\x74\x8a", "\x8a\x74", UINT16_C(0x748a)},
        {"\x7a\x9d", "\x9d\x7a", UINT16_C(0x7a9d)},
        {"\xae\xf2", "\xf2\xae", UINT16_C(0xaef2)},
        {"\x5d\xcd", "\xcd\x5d", UINT16_C(0x5dcd)},
        {"\x1b\xee", "\xee\x1b", UINT16_C(0x1bee)},
        {"\xa1\xfc", "\xfc\xa1", UINT16_C(0xa1fc)},
        {"\xa1\x87", "\x87\xa1", UINT16_C(0xa187)},
    };
    uint16_t natl, natb, bign, litn;
    uint8_t bufl[2], bufb[2];
    int ii, fail;

    fail = 0;
    for(ii = 0; sizeof(tests) / sizeof(tests[0]) > ii; ii++)
    {
        assert(sizeof bign == 2);
        natl = UP_GETBUF16LE(tests[ii].lit);
        natb = UP_GETBUF16BE(tests[ii].big);
        UP_SETBUF16LE(bufl, tests[ii].native);
        UP_SETBUF16BE(bufb, tests[ii].native);
        memcpy(&bign, tests[ii].big, 2);
        memcpy(&litn, tests[ii].lit, 2);

        if(natl == tests[ii].native &&
           natb == tests[ii].native &&
           !memcmp(bufl, tests[ii].lit, 2) &&
           !memcmp(bufb, tests[ii].big, 2) &&
           litn == UP_HTOLE16(tests[ii].native) &&
           bign == UP_HTOBE16(tests[ii].native) &&
           tests[ii].native == UP_LETOH16(litn) &&
           tests[ii].native == UP_BETOH16(bign))
            continue;

        fail = 1;
        printf("16/%d\t", ii);
        if(natl != tests[ii].native)
            printf(" getle:(%04"PRIx16"!=%04"PRIx16")", natl, tests[ii].native);
        if(natb != tests[ii].native)
            printf(" getbe:(%04"PRIx16"!=%04"PRIx16")", natb, tests[ii].native);
        if(memcmp(bufl, tests[ii].lit, 2))
            printf(" setle:(%02x%02x%02x%02x!=%02x%02x%02x%02x)",
                   bufl[0], bufl[1], bufl[2], bufl[3],
                   tests[ii].lit[0], tests[ii].lit[1], tests[ii].lit[2], tests[ii].lit[3]);
        if(memcmp(bufb, tests[ii].big, 2))
            printf(" setle:(%02x%02x%02x%02x!=%02x%02x%02x%02x)",
                   bufb[0], bufb[1], bufb[2], bufb[3],
                   tests[ii].big[0], tests[ii].big[1], tests[ii].big[2], tests[ii].big[3]);
        if(litn != UP_HTOLE16(tests[ii].native))
            printf(" htole:(%04"PRIx16"!=%04"PRIx16")",
                   litn, UP_HTOLE16(tests[ii].native));
        if(bign != UP_HTOBE16(tests[ii].native))
            printf(" htobe:(%04"PRIx16"!=%04"PRIx16")",
                   bign, UP_HTOBE16(tests[ii].native));
        if(tests[ii].native != UP_LETOH16(litn))
            printf(" letoh:(%04"PRIx16"!=%04"PRIx16")",
                   tests[ii].native, UP_LETOH16(litn));
        if(tests[ii].native != UP_BETOH16(bign))
            printf(" betoh:(%04"PRIx16"!=%04"PRIx16")",
                   tests[ii].native, UP_BETOH16(bign));
        putc('\n', stdout);
    }

    return fail;
}

int
dotest32(void)
{
    struct { char *big; char *lit; uint32_t native; } tests[] = {
        {"\xd7\x00\xe4\xd3", "\xd3\xe4\x00\xd7", UINT32_C(0xd700e4d3)},
        {"\x9a\xb4\xb5\x01", "\x01\xb5\xb4\x9a", UINT32_C(0x9ab4b501)},
        {"\x2c\x3b\x33\xce", "\xce\x33\x3b\x2c", UINT32_C(0x2c3b33ce)},
        {"\x39\x2b\xf6\xe0", "\xe0\xf6\x2b\x39", UINT32_C(0x392bf6e0)},
        {"\x9d\x61\xbb\x0e", "\x0e\xbb\x61\x9d", UINT32_C(0x9d61bb0e)},
        {"\x74\x6c\x57\x65", "\x65\x57\x6c\x74", UINT32_C(0x746c5765)},
        {"\xb0\x16\x6a\x4c", "\x4c\x6a\x16\xb0", UINT32_C(0xb0166a4c)},
        {"\x16\x93\x72\x47", "\x47\x72\x93\x16", UINT32_C(0x16937247)},
        {"\x18\x88\x1a\xca", "\xca\x1a\x88\x18", UINT32_C(0x18881aca)},
        {"\x7f\x57\x81\x57", "\x57\x81\x57\x7f", UINT32_C(0x7f578157)},
        {"\x3b\x2b\xa8\xc7", "\xc7\xa8\x2b\x3b", UINT32_C(0x3b2ba8c7)},
        {"\x63\x0d\xad\xe2", "\xe2\xad\x0d\x63", UINT32_C(0x630dade2)},
        {"\x11\xb5\xf2\x1b", "\x1b\xf2\xb5\x11", UINT32_C(0x11b5f21b)},
        {"\x73\x02\xa6\x54", "\x54\xa6\x02\x73", UINT32_C(0x7302a654)},
        {"\x93\xd0\x24\x5a", "\x5a\x24\xd0\x93", UINT32_C(0x93d0245a)},
        {"\x62\x80\xc8\xbf", "\xbf\xc8\x80\x62", UINT32_C(0x6280c8bf)},
        {"\x6e\xbc\x3a\xbd", "\xbd\x3a\xbc\x6e", UINT32_C(0x6ebc3abd)},
        {"\xf8\x0d\xbd\x90", "\x90\xbd\x0d\xf8", UINT32_C(0xf80dbd90)},
        {"\x53\xd4\x43\xad", "\xad\x43\xd4\x53", UINT32_C(0x53d443ad)},
        {"\x4e\x4a\xac\xd4", "\xd4\xac\x4a\x4e", UINT32_C(0x4e4aacd4)},
        {"\x34\x85\x74\xd3", "\xd3\x74\x85\x34", UINT32_C(0x348574d3)},
        {"\xf1\xd8\x1f\x12", "\x12\x1f\xd8\xf1", UINT32_C(0xf1d81f12)},
        {"\x9c\xd1\x95\xe1", "\xe1\x95\xd1\x9c", UINT32_C(0x9cd195e1)},
        {"\xcd\xbb\x38\xa2", "\xa2\x38\xbb\xcd", UINT32_C(0xcdbb38a2)},
        {"\xed\xf6\x8a\xb3", "\xb3\x8a\xf6\xed", UINT32_C(0xedf68ab3)},
        {"\xf4\x81\x25\xdb", "\xdb\x25\x81\xf4", UINT32_C(0xf48125db)},
        {"\x88\xae\x24\x0a", "\x0a\x24\xae\x88", UINT32_C(0x88ae240a)},
        {"\xe0\xc4\x9e\x1b", "\x1b\x9e\xc4\xe0", UINT32_C(0xe0c49e1b)},
        {"\xe8\x15\xcf\x25", "\x25\xcf\x15\xe8", UINT32_C(0xe815cf25)},
        {"\x06\x17\xe1\x36", "\x36\xe1\x17\x06", UINT32_C(0x0617e136)},
        {"\x1c\x8d\x72\xb9", "\xb9\x72\x8d\x1c", UINT32_C(0x1c8d72b9)},
        {"\xa6\xcd\xa8\xd4", "\xd4\xa8\xcd\xa6", UINT32_C(0xa6cda8d4)},
        {"\x46\xc3\xb9\x20", "\x20\xb9\xc3\x46", UINT32_C(0x46c3b920)},
        {"\x18\x7c\xbd\x39", "\x39\xbd\x7c\x18", UINT32_C(0x187cbd39)},
        {"\x36\xac\x4d\xcd", "\xcd\x4d\xac\x36", UINT32_C(0x36ac4dcd)},
        {"\xb0\x04\x83\xb9", "\xb9\x83\x04\xb0", UINT32_C(0xb00483b9)},
        {"\xc4\x83\x28\x7d", "\x7d\x28\x83\xc4", UINT32_C(0xc483287d)},
        {"\xd1\x11\x60\x4e", "\x4e\x60\x11\xd1", UINT32_C(0xd111604e)},
        {"\xbe\x81\xe3\x33", "\x33\xe3\x81\xbe", UINT32_C(0xbe81e333)},
        {"\x64\x9c\x75\xfb", "\xfb\x75\x9c\x64", UINT32_C(0x649c75fb)},
        {"\xce\xef\xcf\x01", "\x01\xcf\xef\xce", UINT32_C(0xceefcf01)},
        {"\xa5\x9b\x3a\x59", "\x59\x3a\x9b\xa5", UINT32_C(0xa59b3a59)},
        {"\x47\xc1\xba\xce", "\xce\xba\xc1\x47", UINT32_C(0x47c1bace)},
        {"\xcb\x7e\x1a\x19", "\x19\x1a\x7e\xcb", UINT32_C(0xcb7e1a19)},
        {"\xa4\xb3\x43\x74", "\x74\x43\xb3\xa4", UINT32_C(0xa4b34374)},
        {"\xac\x91\x75\xec", "\xec\x75\x91\xac", UINT32_C(0xac9175ec)},
        {"\x20\xff\x17\x90", "\x90\x17\xff\x20", UINT32_C(0x20ff1790)},
        {"\x49\x73\xe8\x0d", "\x0d\xe8\x73\x49", UINT32_C(0x4973e80d)},
        {"\xd5\xb5\x90\x41", "\x41\x90\xb5\xd5", UINT32_C(0xd5b59041)},
        {"\x19\xdd\xee\x09", "\x09\xee\xdd\x19", UINT32_C(0x19ddee09)},
        {"\xc5\x07\xe5\xf1", "\xf1\xe5\x07\xc5", UINT32_C(0xc507e5f1)},
        {"\xaf\xa0\x0b\x3c", "\x3c\x0b\xa0\xaf", UINT32_C(0xafa00b3c)},
        {"\x88\x1c\x29\xbb", "\xbb\x29\x1c\x88", UINT32_C(0x881c29bb)},
        {"\x14\x7e\x8b\x6a", "\x6a\x8b\x7e\x14", UINT32_C(0x147e8b6a)},
        {"\xb8\x46\x85\xa8", "\xa8\x85\x46\xb8", UINT32_C(0xb84685a8)},
        {"\x4a\x89\x9b\x8c", "\x8c\x9b\x89\x4a", UINT32_C(0x4a899b8c)},
        {"\xd1\x0f\xcb\x43", "\x43\xcb\x0f\xd1", UINT32_C(0xd10fcb43)},
        {"\x01\x0a\x4c\x7e", "\x7e\x4c\x0a\x01", UINT32_C(0x010a4c7e)},
        {"\x81\x72\x6f\x7b", "\x7b\x6f\x72\x81", UINT32_C(0x81726f7b)},
        {"\x1e\x09\xc5\x57", "\x57\xc5\x09\x1e", UINT32_C(0x1e09c557)},
        {"\x32\xe6\xcb\xac", "\xac\xcb\xe6\x32", UINT32_C(0x32e6cbac)},
        {"\xe5\x08\x8a\x17", "\x17\x8a\x08\xe5", UINT32_C(0xe5088a17)},
        {"\xdf\x95\x3c\x5c", "\x5c\x3c\x95\xdf", UINT32_C(0xdf953c5c)},
        {"\xa8\xa3\x84\xb8", "\xb8\x84\xa3\xa8", UINT32_C(0xa8a384b8)},
        {"\x46\x58\x45\x2e", "\x2e\x45\x58\x46", UINT32_C(0x4658452e)},
        {"\x72\xcd\xce\x06", "\x06\xce\xcd\x72", UINT32_C(0x72cdce06)},
        {"\x3d\xfb\xe7\x2f", "\x2f\xe7\xfb\x3d", UINT32_C(0x3dfbe72f)},
        {"\x08\x3e\x16\xb7", "\xb7\x16\x3e\x08", UINT32_C(0x083e16b7)},
        {"\x2c\x2d\x8c\x46", "\x46\x8c\x2d\x2c", UINT32_C(0x2c2d8c46)},
        {"\x64\x43\x4d\xea", "\xea\x4d\x43\x64", UINT32_C(0x64434dea)},
        {"\xba\x7f\xf7\x53", "\x53\xf7\x7f\xba", UINT32_C(0xba7ff753)},
        {"\x33\x9c\xa5\xfe", "\xfe\xa5\x9c\x33", UINT32_C(0x339ca5fe)},
        {"\x6c\x40\xa3\x0d", "\x0d\xa3\x40\x6c", UINT32_C(0x6c40a30d)},
        {"\xfe\x12\xf1\xc9", "\xc9\xf1\x12\xfe", UINT32_C(0xfe12f1c9)},
        {"\x52\x54\xe3\x94", "\x94\xe3\x54\x52", UINT32_C(0x5254e394)},
        {"\xd5\x5e\x79\x31", "\x31\x79\x5e\xd5", UINT32_C(0xd55e7931)},
        {"\x63\xbc\xde\xb3", "\xb3\xde\xbc\x63", UINT32_C(0x63bcdeb3)},
        {"\x7d\x82\xe3\x04", "\x04\xe3\x82\x7d", UINT32_C(0x7d82e304)},
        {"\xcc\x81\x6e\xe8", "\xe8\x6e\x81\xcc", UINT32_C(0xcc816ee8)},
        {"\x8e\xc3\x1b\x05", "\x05\x1b\xc3\x8e", UINT32_C(0x8ec31b05)},
        {"\x7b\x50\x69\x6e", "\x6e\x69\x50\x7b", UINT32_C(0x7b50696e)},
        {"\xb9\x86\xa4\x3c", "\x3c\xa4\x86\xb9", UINT32_C(0xb986a43c)},
        {"\xca\x21\x29\x4b", "\x4b\x29\x21\xca", UINT32_C(0xca21294b)},
        {"\xac\x42\x7b\xc3", "\xc3\x7b\x42\xac", UINT32_C(0xac427bc3)},
        {"\x92\xab\x8d\x67", "\x67\x8d\xab\x92", UINT32_C(0x92ab8d67)},
        {"\xb3\xb5\x94\x5b", "\x5b\x94\xb5\xb3", UINT32_C(0xb3b5945b)},
        {"\x3b\x1f\x59\xee", "\xee\x59\x1f\x3b", UINT32_C(0x3b1f59ee)},
        {"\x00\xc0\xd6\x34", "\x34\xd6\xc0\x00", UINT32_C(0x00c0d634)},
        {"\x51\xb5\x8c\xe3", "\xe3\x8c\xb5\x51", UINT32_C(0x51b58ce3)},
        {"\x74\x9a\x0e\x43", "\x43\x0e\x9a\x74", UINT32_C(0x749a0e43)},
        {"\xff\xc9\x87\x3d", "\x3d\x87\xc9\xff", UINT32_C(0xffc9873d)},
        {"\x67\xb6\xd7\x6d", "\x6d\xd7\xb6\x67", UINT32_C(0x67b6d76d)},
        {"\x74\xbc\x6d\x31", "\x31\x6d\xbc\x74", UINT32_C(0x74bc6d31)},
        {"\x29\x76\xb4\xe7", "\xe7\xb4\x76\x29", UINT32_C(0x2976b4e7)},
        {"\x0f\x12\x8f\x4f", "\x4f\x8f\x12\x0f", UINT32_C(0x0f128f4f)},
        {"\xc7\x7f\x5e\xa0", "\xa0\x5e\x7f\xc7", UINT32_C(0xc77f5ea0)},
        {"\xd1\xb3\x26\x56", "\x56\x26\xb3\xd1", UINT32_C(0xd1b32656)},
        {"\x42\x6c\xed\x2a", "\x2a\xed\x6c\x42", UINT32_C(0x426ced2a)},
        {"\x23\x11\xd9\x6b", "\x6b\xd9\x11\x23", UINT32_C(0x2311d96b)},
        {"\xff\x6e\xa8\xd2", "\xd2\xa8\x6e\xff", UINT32_C(0xff6ea8d2)},
        {"\xf6\x97\xba\x47", "\x47\xba\x97\xf6", UINT32_C(0xf697ba47)},
        {"\x5a\x07\x9e\x85", "\x85\x9e\x07\x5a", UINT32_C(0x5a079e85)},
        {"\xb2\x5b\x04\xea", "\xea\x04\x5b\xb2", UINT32_C(0xb25b04ea)},
        {"\x7b\x1c\xc1\xf5", "\xf5\xc1\x1c\x7b", UINT32_C(0x7b1cc1f5)},
        {"\x60\xc6\xfa\xef", "\xef\xfa\xc6\x60", UINT32_C(0x60c6faef)},
        {"\x70\x36\xf0\xb6", "\xb6\xf0\x36\x70", UINT32_C(0x7036f0b6)},
        {"\xf9\xf7\xe7\x00", "\x00\xe7\xf7\xf9", UINT32_C(0xf9f7e700)},
        {"\xf8\xd8\x94\xb7", "\xb7\x94\xd8\xf8", UINT32_C(0xf8d894b7)},
        {"\xa5\xe6\x1d\xbd", "\xbd\x1d\xe6\xa5", UINT32_C(0xa5e61dbd)},
        {"\x82\x3a\x8d\x43", "\x43\x8d\x3a\x82", UINT32_C(0x823a8d43)},
        {"\x5a\x7e\x9f\xb8", "\xb8\x9f\x7e\x5a", UINT32_C(0x5a7e9fb8)},
        {"\x15\xc2\xec\xe4", "\xe4\xec\xc2\x15", UINT32_C(0x15c2ece4)},
        {"\xe2\xeb\xee\x5d", "\x5d\xee\xeb\xe2", UINT32_C(0xe2ebee5d)},
        {"\x3b\xb3\x60\x1a", "\x1a\x60\xb3\x3b", UINT32_C(0x3bb3601a)},
        {"\xc8\x3e\x39\x44", "\x44\x39\x3e\xc8", UINT32_C(0xc83e3944)},
        {"\xe9\x5d\xee\x42", "\x42\xee\x5d\xe9", UINT32_C(0xe95dee42)},
        {"\x81\x1b\x5d\xa2", "\xa2\x5d\x1b\x81", UINT32_C(0x811b5da2)},
        {"\x4d\x55\x69\xce", "\xce\x69\x55\x4d", UINT32_C(0x4d5569ce)},
        {"\x13\xd2\xa1\x89", "\x89\xa1\xd2\x13", UINT32_C(0x13d2a189)},
        {"\x72\x42\x78\xb9", "\xb9\x78\x42\x72", UINT32_C(0x724278b9)},
        {"\xa1\xf7\x53\x58", "\x58\x53\xf7\xa1", UINT32_C(0xa1f75358)},
        {"\x7d\x54\xc5\x4d", "\x4d\xc5\x54\x7d", UINT32_C(0x7d54c54d)},
        {"\x09\x2f\xb0\x30", "\x30\xb0\x2f\x09", UINT32_C(0x092fb030)},
        {"\x2b\xa3\x52\x84", "\x84\x52\xa3\x2b", UINT32_C(0x2ba35284)},
        {"\xf6\x0d\xf4\xc5", "\xc5\xf4\x0d\xf6", UINT32_C(0xf60df4c5)},
        {"\xc5\xb3\x2c\x5c", "\x5c\x2c\xb3\xc5", UINT32_C(0xc5b32c5c)},
        {"\xbb\xf0\x8b\x1f", "\x1f\x8b\xf0\xbb", UINT32_C(0xbbf08b1f)},
        {"\x0a\x89\xe9\x6b", "\x6b\xe9\x89\x0a", UINT32_C(0x0a89e96b)},
    };
    uint32_t natl, natb, bign, litn;
    uint8_t bufl[4], bufb[4];
    int ii, fail;

    fail = 0;
    for(ii = 0; sizeof(tests) / sizeof(tests[0]) > ii; ii++)
    {
        assert(sizeof bign == 4);
        natl = UP_GETBUF32LE(tests[ii].lit);
        natb = UP_GETBUF32BE(tests[ii].big);
        UP_SETBUF32LE(bufl, tests[ii].native);
        UP_SETBUF32BE(bufb, tests[ii].native);
        memcpy(&bign, tests[ii].big, 4);
        memcpy(&litn, tests[ii].lit, 4);

        if(natl == tests[ii].native &&
           natb == tests[ii].native &&
           !memcmp(bufl, tests[ii].lit, 4) &&
           !memcmp(bufb, tests[ii].big, 4) &&
           litn == UP_HTOLE32(tests[ii].native) &&
           bign == UP_HTOBE32(tests[ii].native) &&
           tests[ii].native == UP_LETOH32(litn) &&
           tests[ii].native == UP_BETOH32(bign))
            continue;

        fail = 1;
        printf("32/%d\t", ii);
        if(natl != tests[ii].native)
            printf(" getle:(%08"PRIx32"!=%08"PRIx32")", natl, tests[ii].native);
        if(natb != tests[ii].native)
            printf(" getbe:(%08"PRIx32"!=%08"PRIx32")", natb, tests[ii].native);
        if(memcmp(bufl, tests[ii].lit, 4))
            printf(" setle:(%02x%02x%02x%02x%02x%02x%02x%02x!=%02x%02x%02x%02x%02x%02x%02x%02x)",
                   bufl[0], bufl[1], bufl[2], bufl[3], bufl[4], bufl[5], bufl[6], bufl[7],
                   tests[ii].lit[0], tests[ii].lit[1], tests[ii].lit[2], tests[ii].lit[3], tests[ii].lit[4], tests[ii].lit[5], tests[ii].lit[6], tests[ii].lit[7]);
        if(memcmp(bufb, tests[ii].big, 4))
            printf(" setle:(%02x%02x%02x%02x%02x%02x%02x%02x!=%02x%02x%02x%02x%02x%02x%02x%02x)",
                   bufb[0], bufb[1], bufb[2], bufb[3], bufb[4], bufb[5], bufb[6], bufb[7],
                   tests[ii].big[0], tests[ii].big[1], tests[ii].big[2], tests[ii].big[3], tests[ii].big[4], tests[ii].big[5], tests[ii].big[6], tests[ii].big[7]);
        if(litn != UP_HTOLE32(tests[ii].native))
            printf(" htole:(%08"PRIx32"!=%08"PRIx32")",
                   litn, UP_HTOLE32(tests[ii].native));
        if(bign != UP_HTOBE32(tests[ii].native))
            printf(" htobe:(%08"PRIx32"!=%08"PRIx32")",
                   bign, UP_HTOBE32(tests[ii].native));
        if(tests[ii].native != UP_LETOH32(litn))
            printf(" letoh:(%08"PRIx32"!=%08"PRIx32")",
                   tests[ii].native, UP_LETOH32(litn));
        if(tests[ii].native != UP_BETOH32(bign))
            printf(" betoh:(%08"PRIx32"!=%08"PRIx32")",
                   tests[ii].native, UP_BETOH32(bign));
        putc('\n', stdout);
    }

    return fail;
}

int
dotest64(void)
{
    struct { char *big; char *lit; uint64_t native; } tests[] = {
        {"\xeb\xc8\x59\xe0\x1b\x4b\x60\x62", "\x62\x60\x4b\x1b\xe0\x59\xc8\xeb", UINT64_C(0xebc859e01b4b6062)},
        {"\xc3\x88\x19\xce\x87\x39\x8b\xdd", "\xdd\x8b\x39\x87\xce\x19\x88\xc3", UINT64_C(0xc38819ce87398bdd)},
        {"\xf3\x31\x1c\xeb\xd3\x76\x28\x41", "\x41\x28\x76\xd3\xeb\x1c\x31\xf3", UINT64_C(0xf3311cebd3762841)},
        {"\x8e\xf7\x6b\xa3\x1e\xd4\x8f\x6c", "\x6c\x8f\xd4\x1e\xa3\x6b\xf7\x8e", UINT64_C(0x8ef76ba31ed48f6c)},
        {"\xa7\x0c\x72\x33\x8d\x45\x26\xf0", "\xf0\x26\x45\x8d\x33\x72\x0c\xa7", UINT64_C(0xa70c72338d4526f0)},
        {"\xf6\x4a\x1e\x7f\xfe\x7c\x54\x9b", "\x9b\x54\x7c\xfe\x7f\x1e\x4a\xf6", UINT64_C(0xf64a1e7ffe7c549b)},
        {"\x69\x60\x39\x19\xe0\x4f\xa5\xbe", "\xbe\xa5\x4f\xe0\x19\x39\x60\x69", UINT64_C(0x69603919e04fa5be)},
        {"\x96\x6a\x87\x2f\x82\x26\x71\x4d", "\x4d\x71\x26\x82\x2f\x87\x6a\x96", UINT64_C(0x966a872f8226714d)},
        {"\xa0\xfd\x5c\xb9\xe6\x4d\x6d\xbf", "\xbf\x6d\x4d\xe6\xb9\x5c\xfd\xa0", UINT64_C(0xa0fd5cb9e64d6dbf)},
        {"\x00\xf9\xa9\x58\x08\x91\xa4\xf4", "\xf4\xa4\x91\x08\x58\xa9\xf9\x00", UINT64_C(0x00f9a9580891a4f4)},
        {"\x8d\x2f\xff\xda\x4e\x29\x70\x4c", "\x4c\x70\x29\x4e\xda\xff\x2f\x8d", UINT64_C(0x8d2fffda4e29704c)},
        {"\x58\x84\x1a\x29\xe3\x77\xfe\xdc", "\xdc\xfe\x77\xe3\x29\x1a\x84\x58", UINT64_C(0x58841a29e377fedc)},
        {"\xea\xad\x99\x52\xe7\x93\x41\x48", "\x48\x41\x93\xe7\x52\x99\xad\xea", UINT64_C(0xeaad9952e7934148)},
        {"\x8f\xe6\x44\x45\x0a\x9e\x54\x26", "\x26\x54\x9e\x0a\x45\x44\xe6\x8f", UINT64_C(0x8fe644450a9e5426)},
        {"\x74\x53\xee\x93\x02\x2f\x9a\x8d", "\x8d\x9a\x2f\x02\x93\xee\x53\x74", UINT64_C(0x7453ee93022f9a8d)},
        {"\xb1\x36\x95\x48\xba\xda\x83\xa6", "\xa6\x83\xda\xba\x48\x95\x36\xb1", UINT64_C(0xb1369548bada83a6)},
        {"\x97\xe9\x72\x50\xdc\x96\xbc\x7c", "\x7c\xbc\x96\xdc\x50\x72\xe9\x97", UINT64_C(0x97e97250dc96bc7c)},
        {"\x3a\xee\xf6\xbb\x75\xae\xee\xfc", "\xfc\xee\xae\x75\xbb\xf6\xee\x3a", UINT64_C(0x3aeef6bb75aeeefc)},
        {"\xe6\xa7\x99\x4d\x35\xfb\xcf\x73", "\x73\xcf\xfb\x35\x4d\x99\xa7\xe6", UINT64_C(0xe6a7994d35fbcf73)},
        {"\xb8\x01\x2c\xbd\x7c\xc1\xfa\x26", "\x26\xfa\xc1\x7c\xbd\x2c\x01\xb8", UINT64_C(0xb8012cbd7cc1fa26)},
        {"\x7c\x9a\x5d\x6b\x17\xa8\x18\x9e", "\x9e\x18\xa8\x17\x6b\x5d\x9a\x7c", UINT64_C(0x7c9a5d6b17a8189e)},
        {"\xc3\x05\xe5\x0f\x95\x04\xae\xfd", "\xfd\xae\x04\x95\x0f\xe5\x05\xc3", UINT64_C(0xc305e50f9504aefd)},
        {"\x57\x44\x6a\xbb\x65\x65\x23\x9d", "\x9d\x23\x65\x65\xbb\x6a\x44\x57", UINT64_C(0x57446abb6565239d)},
        {"\x9d\x83\x9a\xf5\x09\xe3\x8d\x21", "\x21\x8d\xe3\x09\xf5\x9a\x83\x9d", UINT64_C(0x9d839af509e38d21)},
        {"\x3b\x13\xfb\x46\xb3\xfa\xbd\x0f", "\x0f\xbd\xfa\xb3\x46\xfb\x13\x3b", UINT64_C(0x3b13fb46b3fabd0f)},
        {"\x5b\xac\x50\xb8\xcc\xe1\xee\x94", "\x94\xee\xe1\xcc\xb8\x50\xac\x5b", UINT64_C(0x5bac50b8cce1ee94)},
        {"\x29\x39\xe6\x04\x33\xf5\x86\xa4", "\xa4\x86\xf5\x33\x04\xe6\x39\x29", UINT64_C(0x2939e60433f586a4)},
        {"\x15\x53\x05\x3e\x24\x7a\xa1\x17", "\x17\xa1\x7a\x24\x3e\x05\x53\x15", UINT64_C(0x1553053e247aa117)},
        {"\x43\x6f\x56\xbd\x59\xe5\x2a\xe6", "\xe6\x2a\xe5\x59\xbd\x56\x6f\x43", UINT64_C(0x436f56bd59e52ae6)},
        {"\xd9\xee\x68\xd7\xa6\x49\xc2\x87", "\x87\xc2\x49\xa6\xd7\x68\xee\xd9", UINT64_C(0xd9ee68d7a649c287)},
        {"\xb3\x06\x9a\xc3\xa3\x14\x31\xbe", "\xbe\x31\x14\xa3\xc3\x9a\x06\xb3", UINT64_C(0xb3069ac3a31431be)},
        {"\x2e\x65\xb5\x3b\x46\xa7\xd3\x18", "\x18\xd3\xa7\x46\x3b\xb5\x65\x2e", UINT64_C(0x2e65b53b46a7d318)},
        {"\x4c\x55\x77\x96\x1c\x4b\x5b\x9d", "\x9d\x5b\x4b\x1c\x96\x77\x55\x4c", UINT64_C(0x4c5577961c4b5b9d)},
        {"\x0f\xa7\xe5\xf9\x39\x47\x31\x4d", "\x4d\x31\x47\x39\xf9\xe5\xa7\x0f", UINT64_C(0x0fa7e5f93947314d)},
        {"\x5a\xa9\x58\x96\xa7\x2e\x0f\xb4", "\xb4\x0f\x2e\xa7\x96\x58\xa9\x5a", UINT64_C(0x5aa95896a72e0fb4)},
        {"\xf4\x33\x6e\xa2\xe7\xa0\x39\xc1", "\xc1\x39\xa0\xe7\xa2\x6e\x33\xf4", UINT64_C(0xf4336ea2e7a039c1)},
        {"\x14\xb5\x3b\x7e\x7e\xc6\xda\x15", "\x15\xda\xc6\x7e\x7e\x3b\xb5\x14", UINT64_C(0x14b53b7e7ec6da15)},
        {"\x23\xd3\xf9\x50\x1d\xa2\xc7\x78", "\x78\xc7\xa2\x1d\x50\xf9\xd3\x23", UINT64_C(0x23d3f9501da2c778)},
        {"\x38\xb4\x63\x01\xaf\x36\xc1\x7a", "\x7a\xc1\x36\xaf\x01\x63\xb4\x38", UINT64_C(0x38b46301af36c17a)},
        {"\x49\x37\x8b\xf6\x67\x94\xd7\x97", "\x97\xd7\x94\x67\xf6\x8b\x37\x49", UINT64_C(0x49378bf66794d797)},
        {"\xe9\x99\xb3\x3c\x1d\xd7\xb9\x8a", "\x8a\xb9\xd7\x1d\x3c\xb3\x99\xe9", UINT64_C(0xe999b33c1dd7b98a)},
        {"\x9e\x33\xa5\x2b\xbb\x73\x88\xe3", "\xe3\x88\x73\xbb\x2b\xa5\x33\x9e", UINT64_C(0x9e33a52bbb7388e3)},
        {"\x5f\x2f\x29\x41\xef\x65\x32\xe2", "\xe2\x32\x65\xef\x41\x29\x2f\x5f", UINT64_C(0x5f2f2941ef6532e2)},
        {"\x69\xfd\xa9\xe7\x5f\x23\x7c\x98", "\x98\x7c\x23\x5f\xe7\xa9\xfd\x69", UINT64_C(0x69fda9e75f237c98)},
        {"\x39\x13\x5b\x28\x18\x0d\xec\x60", "\x60\xec\x0d\x18\x28\x5b\x13\x39", UINT64_C(0x39135b28180dec60)},
        {"\x60\x0e\x64\x1e\x41\xde\xc0\x6d", "\x6d\xc0\xde\x41\x1e\x64\x0e\x60", UINT64_C(0x600e641e41dec06d)},
        {"\x56\x92\x03\xb8\x5a\x9a\xf3\xcb", "\xcb\xf3\x9a\x5a\xb8\x03\x92\x56", UINT64_C(0x569203b85a9af3cb)},
        {"\x4f\x4b\x20\x19\x5e\x11\xab\xbb", "\xbb\xab\x11\x5e\x19\x20\x4b\x4f", UINT64_C(0x4f4b20195e11abbb)},
        {"\x2d\x54\x78\x0f\xad\x7e\xd7\x38", "\x38\xd7\x7e\xad\x0f\x78\x54\x2d", UINT64_C(0x2d54780fad7ed738)},
        {"\xb1\x60\x89\xcc\xcd\xe4\xe0\x11", "\x11\xe0\xe4\xcd\xcc\x89\x60\xb1", UINT64_C(0xb16089cccde4e011)},
        {"\x67\xc3\xdf\xf3\x4c\x92\x38\x0b", "\x0b\x38\x92\x4c\xf3\xdf\xc3\x67", UINT64_C(0x67c3dff34c92380b)},
        {"\x8d\x7a\x46\x09\x3a\x21\x13\x82", "\x82\x13\x21\x3a\x09\x46\x7a\x8d", UINT64_C(0x8d7a46093a211382)},
        {"\x9e\x9e\x27\x7d\x1d\x6a\x67\xa8", "\xa8\x67\x6a\x1d\x7d\x27\x9e\x9e", UINT64_C(0x9e9e277d1d6a67a8)},
        {"\xc6\x3e\xdb\x76\xbd\x9b\xb4\x3e", "\x3e\xb4\x9b\xbd\x76\xdb\x3e\xc6", UINT64_C(0xc63edb76bd9bb43e)},
        {"\xd6\x9b\x31\xb2\xe9\x26\x99\x83", "\x83\x99\x26\xe9\xb2\x31\x9b\xd6", UINT64_C(0xd69b31b2e9269983)},
        {"\x9a\x6e\x47\xb1\x98\x24\xaf\x59", "\x59\xaf\x24\x98\xb1\x47\x6e\x9a", UINT64_C(0x9a6e47b19824af59)},
        {"\xb6\xf7\x2f\x97\x48\x69\x35\x7a", "\x7a\x35\x69\x48\x97\x2f\xf7\xb6", UINT64_C(0xb6f72f974869357a)},
        {"\x7a\x46\x6b\x66\xb7\x72\x4f\xa7", "\xa7\x4f\x72\xb7\x66\x6b\x46\x7a", UINT64_C(0x7a466b66b7724fa7)},
        {"\x85\x0f\x11\xe7\x90\xd0\xa2\xcb", "\xcb\xa2\xd0\x90\xe7\x11\x0f\x85", UINT64_C(0x850f11e790d0a2cb)},
        {"\x59\xca\xb5\x70\x3a\x53\x59\xc6", "\xc6\x59\x53\x3a\x70\xb5\xca\x59", UINT64_C(0x59cab5703a5359c6)},
        {"\xa2\x9f\x5a\x22\x2b\x94\x20\x48", "\x48\x20\x94\x2b\x22\x5a\x9f\xa2", UINT64_C(0xa29f5a222b942048)},
        {"\x8d\x6d\x71\x74\xc2\x0b\x2b\x8a", "\x8a\x2b\x0b\xc2\x74\x71\x6d\x8d", UINT64_C(0x8d6d7174c20b2b8a)},
        {"\x66\x5b\xf6\x16\x5e\x76\x9a\xcc", "\xcc\x9a\x76\x5e\x16\xf6\x5b\x66", UINT64_C(0x665bf6165e769acc)},
        {"\x01\xa1\xa7\xb9\x8d\xd8\x2d\x70", "\x70\x2d\xd8\x8d\xb9\xa7\xa1\x01", UINT64_C(0x01a1a7b98dd82d70)},
        {"\x68\x51\x6f\x5e\x7e\xdb\x39\xcd", "\xcd\x39\xdb\x7e\x5e\x6f\x51\x68", UINT64_C(0x68516f5e7edb39cd)},
        {"\x29\x65\x10\xf5\xb7\x30\xe6\x8f", "\x8f\xe6\x30\xb7\xf5\x10\x65\x29", UINT64_C(0x296510f5b730e68f)},
        {"\x9a\xa1\x8d\x12\xe9\x0f\x76\xd0", "\xd0\x76\x0f\xe9\x12\x8d\xa1\x9a", UINT64_C(0x9aa18d12e90f76d0)},
        {"\xc7\xbd\xbf\xf0\x26\x61\xa7\x99", "\x99\xa7\x61\x26\xf0\xbf\xbd\xc7", UINT64_C(0xc7bdbff02661a799)},
        {"\xe3\x4e\x3b\x9f\xa4\x71\xa9\x21", "\x21\xa9\x71\xa4\x9f\x3b\x4e\xe3", UINT64_C(0xe34e3b9fa471a921)},
        {"\x94\xda\x4a\x3d\x1f\xbe\x55\xe5", "\xe5\x55\xbe\x1f\x3d\x4a\xda\x94", UINT64_C(0x94da4a3d1fbe55e5)},
        {"\xce\x7a\x14\x85\x65\xfd\xee\xe8", "\xe8\xee\xfd\x65\x85\x14\x7a\xce", UINT64_C(0xce7a148565fdeee8)},
        {"\x4c\x7d\xf9\xd2\xd8\x5b\x21\x50", "\x50\x21\x5b\xd8\xd2\xf9\x7d\x4c", UINT64_C(0x4c7df9d2d85b2150)},
        {"\x19\x60\x66\x88\xf5\x15\x9d\x0e", "\x0e\x9d\x15\xf5\x88\x66\x60\x19", UINT64_C(0x19606688f5159d0e)},
        {"\x6f\x21\x38\xa2\x9b\x48\xe3\xc5", "\xc5\xe3\x48\x9b\xa2\x38\x21\x6f", UINT64_C(0x6f2138a29b48e3c5)},
        {"\x96\xef\x76\xd5\x50\xcc\x34\xc8", "\xc8\x34\xcc\x50\xd5\x76\xef\x96", UINT64_C(0x96ef76d550cc34c8)},
        {"\xca\x8a\xa7\xcd\x6c\xe2\x6e\xdf", "\xdf\x6e\xe2\x6c\xcd\xa7\x8a\xca", UINT64_C(0xca8aa7cd6ce26edf)},
        {"\xb3\x72\xd7\x04\xa3\xae\x17\xd0", "\xd0\x17\xae\xa3\x04\xd7\x72\xb3", UINT64_C(0xb372d704a3ae17d0)},
        {"\xcf\x43\x4b\x22\x51\x52\x0a\x9b", "\x9b\x0a\x52\x51\x22\x4b\x43\xcf", UINT64_C(0xcf434b2251520a9b)},
        {"\xde\x3f\x15\x09\xd7\xf4\x1c\x14", "\x14\x1c\xf4\xd7\x09\x15\x3f\xde", UINT64_C(0xde3f1509d7f41c14)},
        {"\x4d\xa0\xf1\xfe\x80\x5d\x08\xc9", "\xc9\x08\x5d\x80\xfe\xf1\xa0\x4d", UINT64_C(0x4da0f1fe805d08c9)},
        {"\x3c\x78\x2a\x2f\x29\x0a\x98\x24", "\x24\x98\x0a\x29\x2f\x2a\x78\x3c", UINT64_C(0x3c782a2f290a9824)},
        {"\xaf\x4a\x1f\x6d\x58\x99\xb2\xa8", "\xa8\xb2\x99\x58\x6d\x1f\x4a\xaf", UINT64_C(0xaf4a1f6d5899b2a8)},
        {"\x6e\xa8\xc8\xff\x9e\xd0\xe6\xf1", "\xf1\xe6\xd0\x9e\xff\xc8\xa8\x6e", UINT64_C(0x6ea8c8ff9ed0e6f1)},
        {"\xb3\x27\x92\xf4\xfe\x3c\x1e\xa9", "\xa9\x1e\x3c\xfe\xf4\x92\x27\xb3", UINT64_C(0xb32792f4fe3c1ea9)},
        {"\xab\x2d\x20\x0b\x14\x18\x8a\xdb", "\xdb\x8a\x18\x14\x0b\x20\x2d\xab", UINT64_C(0xab2d200b14188adb)},
        {"\xac\xdc\xde\x0f\xa6\x8f\x5d\x1a", "\x1a\x5d\x8f\xa6\x0f\xde\xdc\xac", UINT64_C(0xacdcde0fa68f5d1a)},
        {"\x74\x19\x6f\x62\xee\xba\x8a\xcd", "\xcd\x8a\xba\xee\x62\x6f\x19\x74", UINT64_C(0x74196f62eeba8acd)},
        {"\x79\x05\x28\x05\x78\x4a\x8c\x26", "\x26\x8c\x4a\x78\x05\x28\x05\x79", UINT64_C(0x79052805784a8c26)},
        {"\x96\x61\x2c\x7c\x55\x2c\x1a\xd6", "\xd6\x1a\x2c\x55\x7c\x2c\x61\x96", UINT64_C(0x96612c7c552c1ad6)},
        {"\x3a\xde\x54\x5c\x71\xd0\x97\x31", "\x31\x97\xd0\x71\x5c\x54\xde\x3a", UINT64_C(0x3ade545c71d09731)},
        {"\x02\x19\x12\xb3\xe5\x60\x1d\xc4", "\xc4\x1d\x60\xe5\xb3\x12\x19\x02", UINT64_C(0x021912b3e5601dc4)},
        {"\x84\x9e\x31\x0a\x4d\x6f\x0a\xa2", "\xa2\x0a\x6f\x4d\x0a\x31\x9e\x84", UINT64_C(0x849e310a4d6f0aa2)},
        {"\x25\x97\x5b\xa6\xab\x6f\xf6\x7e", "\x7e\xf6\x6f\xab\xa6\x5b\x97\x25", UINT64_C(0x25975ba6ab6ff67e)},
        {"\x5e\x55\xaa\xe8\x53\xa8\x2b\x8a", "\x8a\x2b\xa8\x53\xe8\xaa\x55\x5e", UINT64_C(0x5e55aae853a82b8a)},
        {"\xdc\xe7\x6a\xe0\x23\x24\x26\x20", "\x20\x26\x24\x23\xe0\x6a\xe7\xdc", UINT64_C(0xdce76ae023242620)},
        {"\x33\x01\xd4\x40\x24\x2a\xf6\x40", "\x40\xf6\x2a\x24\x40\xd4\x01\x33", UINT64_C(0x3301d440242af640)},
        {"\x61\xd9\x2e\x08\x88\x1c\xe3\x75", "\x75\xe3\x1c\x88\x08\x2e\xd9\x61", UINT64_C(0x61d92e08881ce375)},
        {"\x65\xe4\x1c\x10\xe6\xd8\xf8\xf3", "\xf3\xf8\xd8\xe6\x10\x1c\xe4\x65", UINT64_C(0x65e41c10e6d8f8f3)},
        {"\xfd\xf9\xd4\x7a\x81\xc8\x9f\x67", "\x67\x9f\xc8\x81\x7a\xd4\xf9\xfd", UINT64_C(0xfdf9d47a81c89f67)},
        {"\xd2\xd1\x35\x41\xc1\xa8\x0b\xa6", "\xa6\x0b\xa8\xc1\x41\x35\xd1\xd2", UINT64_C(0xd2d13541c1a80ba6)},
        {"\x44\xd8\x6c\x87\xc6\x96\xda\x7a", "\x7a\xda\x96\xc6\x87\x6c\xd8\x44", UINT64_C(0x44d86c87c696da7a)},
        {"\x51\x88\xe4\xc7\x63\xb2\xc0\x97", "\x97\xc0\xb2\x63\xc7\xe4\x88\x51", UINT64_C(0x5188e4c763b2c097)},
        {"\xc2\x31\x61\x10\x33\xf0\xa9\xf5", "\xf5\xa9\xf0\x33\x10\x61\x31\xc2", UINT64_C(0xc231611033f0a9f5)},
        {"\x47\x02\xca\x36\xd4\x5c\x42\x7e", "\x7e\x42\x5c\xd4\x36\xca\x02\x47", UINT64_C(0x4702ca36d45c427e)},
        {"\x06\x24\x6a\x10\xe3\xd4\x4a\x99", "\x99\x4a\xd4\xe3\x10\x6a\x24\x06", UINT64_C(0x06246a10e3d44a99)},
        {"\x2c\xe4\xa8\x46\x24\x1a\x5c\xff", "\xff\x5c\x1a\x24\x46\xa8\xe4\x2c", UINT64_C(0x2ce4a846241a5cff)},
        {"\xa3\x3e\x77\xa8\x60\x9c\x5d\x50", "\x50\x5d\x9c\x60\xa8\x77\x3e\xa3", UINT64_C(0xa33e77a8609c5d50)},
        {"\x53\x22\xd7\x4e\x85\x0a\x67\x43", "\x43\x67\x0a\x85\x4e\xd7\x22\x53", UINT64_C(0x5322d74e850a6743)},
        {"\x2e\xad\xe7\x7b\x24\xc4\x81\x10", "\x10\x81\xc4\x24\x7b\xe7\xad\x2e", UINT64_C(0x2eade77b24c48110)},
        {"\x7b\x63\x1d\x73\x12\x91\x6b\x73", "\x73\x6b\x91\x12\x73\x1d\x63\x7b", UINT64_C(0x7b631d7312916b73)},
        {"\x83\xd7\xdc\xe8\x28\x99\x14\xe9", "\xe9\x14\x99\x28\xe8\xdc\xd7\x83", UINT64_C(0x83d7dce8289914e9)},
        {"\x02\xf9\x1f\x04\xeb\xca\x61\x1b", "\x1b\x61\xca\xeb\x04\x1f\xf9\x02", UINT64_C(0x02f91f04ebca611b)},
        {"\x6b\x70\x50\x40\xd8\x7f\xe3\xdf", "\xdf\xe3\x7f\xd8\x40\x50\x70\x6b", UINT64_C(0x6b705040d87fe3df)},
        {"\x3a\xb1\x12\x3a\xd3\x9c\xe9\xb8", "\xb8\xe9\x9c\xd3\x3a\x12\xb1\x3a", UINT64_C(0x3ab1123ad39ce9b8)},
        {"\xe3\xb3\xb8\x7a\x67\x49\x44\x07", "\x07\x44\x49\x67\x7a\xb8\xb3\xe3", UINT64_C(0xe3b3b87a67494407)},
        {"\x69\x79\x43\x66\x7c\x9d\x8e\x99", "\x99\x8e\x9d\x7c\x66\x43\x79\x69", UINT64_C(0x697943667c9d8e99)},
        {"\xd5\x42\x49\x34\x68\xfc\x85\x8c", "\x8c\x85\xfc\x68\x34\x49\x42\xd5", UINT64_C(0xd542493468fc858c)},
        {"\x9d\xc0\x28\xa2\xe5\x24\xa1\xe7", "\xe7\xa1\x24\xe5\xa2\x28\xc0\x9d", UINT64_C(0x9dc028a2e524a1e7)},
        {"\x30\x89\xfe\xeb\x77\x25\x15\xfe", "\xfe\x15\x25\x77\xeb\xfe\x89\x30", UINT64_C(0x3089feeb772515fe)},
        {"\x09\xdf\x12\x4b\xb6\xa4\xc6\x15", "\x15\xc6\xa4\xb6\x4b\x12\xdf\x09", UINT64_C(0x09df124bb6a4c615)},
        {"\xc2\xab\xf7\x25\xea\xfd\xff\xc9", "\xc9\xff\xfd\xea\x25\xf7\xab\xc2", UINT64_C(0xc2abf725eafdffc9)},
        {"\xbe\x9f\x97\x5b\xce\x84\x4a\x0c", "\x0c\x4a\x84\xce\x5b\x97\x9f\xbe", UINT64_C(0xbe9f975bce844a0c)},
        {"\x41\xf8\xbe\x3c\x24\x16\xd2\xd3", "\xd3\xd2\x16\x24\x3c\xbe\xf8\x41", UINT64_C(0x41f8be3c2416d2d3)},
        {"\x10\xd8\xb3\x0d\x2a\x00\x80\x9b", "\x9b\x80\x00\x2a\x0d\xb3\xd8\x10", UINT64_C(0x10d8b30d2a00809b)},
        {"\xbe\x2f\x2a\xa1\x40\xc5\x04\xe3", "\xe3\x04\xc5\x40\xa1\x2a\x2f\xbe", UINT64_C(0xbe2f2aa140c504e3)},
        {"\xe2\x5d\xf8\x6b\x8e\x40\xfb\x04", "\x04\xfb\x40\x8e\x6b\xf8\x5d\xe2", UINT64_C(0xe25df86b8e40fb04)},
        {"\xc4\x91\xa3\xd2\x66\xf7\xdc\x9d", "\x9d\xdc\xf7\x66\xd2\xa3\x91\xc4", UINT64_C(0xc491a3d266f7dc9d)},
        {"\xb6\xfc\x09\xf3\x6a\x36\xc5\x01", "\x01\xc5\x36\x6a\xf3\x09\xfc\xb6", UINT64_C(0xb6fc09f36a36c501)},
    };
    uint64_t natl, natb, bign, litn;
    uint8_t bufl[8], bufb[8];
    int ii, fail;

    fail = 0;
    for(ii = 0; sizeof(tests) / sizeof(tests[0]) > ii; ii++)
    {
        assert(sizeof bign == 8);
        natl = UP_GETBUF64LE(tests[ii].lit);
        natb = UP_GETBUF64BE(tests[ii].big);
        UP_SETBUF64LE(bufl, tests[ii].native);
        UP_SETBUF64BE(bufb, tests[ii].native);
        memcpy(&bign, tests[ii].big, 8);
        memcpy(&litn, tests[ii].lit, 8);

        if(natl == tests[ii].native &&
           natb == tests[ii].native &&
           !memcmp(bufl, tests[ii].lit, 8) &&
           !memcmp(bufb, tests[ii].big, 8) &&
           litn == UP_HTOLE64(tests[ii].native) &&
           bign == UP_HTOBE64(tests[ii].native) &&
           tests[ii].native == UP_LETOH64(litn) &&
           tests[ii].native == UP_BETOH64(bign))
            continue;

        fail = 1;
        printf("64/%d\t", ii);
        if(natl != tests[ii].native)
            printf(" getle:(%016"PRIx64"!=%016"PRIx64")", natl, tests[ii].native);
        if(natb != tests[ii].native)
            printf(" getbe:(%016"PRIx64"!=%016"PRIx64")", natb, tests[ii].native);
        if(memcmp(bufl, tests[ii].lit, 8))
            printf(" setle:(%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x!=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x)",
                   bufl[0], bufl[1], bufl[2], bufl[3], bufl[4], bufl[5], bufl[6], bufl[7], bufl[8], bufl[9], bufl[10], bufl[11], bufl[12], bufl[13], bufl[14], bufl[15],
                   tests[ii].lit[0], tests[ii].lit[1], tests[ii].lit[2], tests[ii].lit[3], tests[ii].lit[4], tests[ii].lit[5], tests[ii].lit[6], tests[ii].lit[7], tests[ii].lit[8], tests[ii].lit[9], tests[ii].lit[10], tests[ii].lit[11], tests[ii].lit[12], tests[ii].lit[13], tests[ii].lit[14], tests[ii].lit[15]);
        if(memcmp(bufb, tests[ii].big, 8))
            printf(" setle:(%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x!=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x)",
                   bufb[0], bufb[1], bufb[2], bufb[3], bufb[4], bufb[5], bufb[6], bufb[7], bufb[8], bufb[9], bufb[10], bufb[11], bufb[12], bufb[13], bufb[14], bufb[15],
                   tests[ii].big[0], tests[ii].big[1], tests[ii].big[2], tests[ii].big[3], tests[ii].big[4], tests[ii].big[5], tests[ii].big[6], tests[ii].big[7], tests[ii].big[8], tests[ii].big[9], tests[ii].big[10], tests[ii].big[11], tests[ii].big[12], tests[ii].big[13], tests[ii].big[14], tests[ii].big[15]);
        if(litn != UP_HTOLE64(tests[ii].native))
            printf(" htole:(%016"PRIx64"!=%016"PRIx64")",
                   litn, UP_HTOLE64(tests[ii].native));
        if(bign != UP_HTOBE64(tests[ii].native))
            printf(" htobe:(%016"PRIx64"!=%016"PRIx64")",
                   bign, UP_HTOBE64(tests[ii].native));
        if(tests[ii].native != UP_LETOH64(litn))
            printf(" letoh:(%016"PRIx64"!=%016"PRIx64")",
                   tests[ii].native, UP_LETOH64(litn));
        if(tests[ii].native != UP_BETOH64(bign))
            printf(" betoh:(%016"PRIx64"!=%016"PRIx64")",
                   tests[ii].native, UP_BETOH64(bign));
        putc('\n', stdout);
    }

    return fail;
}

int
main()
{
    int f16, f32, f64;

    if(0 > up_getendian())
        return 1;

    f16 = dotest16();
    f32 = dotest32();
    f64 = dotest64();

    return f16 || f32 || f64;
}

// '''
