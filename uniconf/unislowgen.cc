/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 2002 Net Integration Technologies, Inc.
 * 
 * A UniConfGen that makes everything slow.  See unislowgen.h.
 */
#include "unislowgen.h"
#include "wvmoniker.h"
#include <unistd.h>


static IUniConfGen *creator(WvStringParm s)
{
    return new UniSlowGen(wvcreate<IUniConfGen>(s));
}
static const UUID uuid = {0xac748a2c, 0x59e8, 0x4ecf,
			  {0x81, 0x9f, 0xe5, 0xc8, 0xc3, 0xa6, 0xc1, 0x3d}};
static WvMoniker<IUniConfGen> reg("slow", uuid, creator);


UniSlowGen::UniSlowGen(IUniConfGen *inner) : UniFilterGen(inner)
{
    slowcount = 0;
}


UniSlowGen::~UniSlowGen()
{
    fprintf(stderr, "%p: UniSlowGen: ran a total of %d slow operations.\n",
	    this, how_slow());
}


void UniSlowGen::commit()
{
    be_slow("commit()");
    UniFilterGen::commit();
}


bool UniSlowGen::refresh()
{
    be_slow("refresh()");
    return UniFilterGen::refresh();
}


WvString UniSlowGen::get(const UniConfKey &key)
{
    be_slow("get(%s)", key);
    return UniFilterGen::get(key);
}


bool UniSlowGen::exists(const UniConfKey &key)
{
    be_slow("exists(%s)", key);
    return UniFilterGen::exists(key);
}


bool UniSlowGen::haschildren(const UniConfKey &key)
{
    be_slow("haschildren(%s)", key);
    return UniFilterGen::haschildren(key);
}


UniConfGen::Iter *UniSlowGen::iterator(const UniConfKey &key)
{
    be_slow("iterator(%s)", key);
    return UniFilterGen::iterator(key);
}


UniConfGen::Iter *UniSlowGen::recursiveiterator(const UniConfKey &key)
{
    be_slow("recursiveiterator(%s)", key);
    return UniFilterGen::recursiveiterator(key);
}


void UniSlowGen::be_slow(WvStringParm what)
{
    fprintf(stderr, "%p: UniSlowGen: slow operation: %s\n",
	    this, what.cstr());
    // sleep(1);
    slowcount++;
}


