/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2002 Net Integration Technologies, Inc.
 * 
 * Defines a UniConfGen that manages a tree of UniConfGen instances.
 */
#ifndef __UNICONFMOUNTTREE_H
#define __UNICONFMOUNTTREE_H

#include "uniconfgen.h"
#include "uniconftree.h"
#include "wvcallback.h"
#include "wvstringtable.h"
#include "wvmoniker.h"

/**
 * Used by UniConfMountTreeGen to maintain information about mounted
 * subtrees.
 */
class UniConfMountTree : public UniConfTree<UniConfMountTree>
{
public:
    UniConfGenList generators;

    UniConfMountTree(UniConfMountTree *parent, const UniConfKey &key);
    ~UniConfMountTree();

    /** Returns true if the node should not be pruned. */
    bool isessential()
        { return haschildren() || ! generators.isempty(); }

    /**
     * Returns the nearest node in the info tree to the key.
     * "key" is the key
     * "split" is set to the number of leading segments used
     * Returns: the node
     */
    UniConfMountTree *findnearest(const UniConfKey &key, int &split);

    /** Finds or makes an info node for the specified key. */
    UniConfMountTree *findormake(const UniConfKey &key);
   
    // an iterator over nodes that have information about a key
    class MountIter;
    // an iterator over generators about a key
    class GenIter;
};


/**
 * An iterator over the UniConfMountTree nodes that might know something
 * about the provided 'key', starting with the nearest match and then
 * moving up the tree.
 */
class UniConfMountTree::MountIter
{
    int bestsplit;
    UniConfMountTree *bestnode;

    int xsplit;
    UniConfMountTree *xnode;
    UniConfKey xkey;

public:
    MountIter(UniConfMountTree &root, const UniConfKey &key);
    
    void rewind();
    bool next();
    
    int split() const
        { return xsplit; }
    UniConfKey key() const
        { return xkey; }
    UniConfKey head() const
        { return xkey.first(xsplit); }
    UniConfKey tail() const
        { return xkey.removefirst(xsplit); }
    UniConfMountTree *node() const
        { return xnode; }
    UniConfMountTree *ptr() const
        { return node(); }
    WvIterStuff(UniConfMountTree);
};


/**
 * An iterator over the generators that might provide a key
 * starting with the nearest match.
 * 
 * eg. if you have something mounted on /foo and /foo/bar/baz, and you ask
 * for a GenIter starting at /foo/bar/baz/boo/snoot, GenIter will give you
 * /foo/bar/baz followed by /foo; MountIter will give you /foo/bar/baz,
 * then /foo/bar, then /foo.
 */
class UniConfMountTree::GenIter : private UniConfMountTree::MountIter
{
    UniConfGenList::Iter *genit; /*!< active generator iterator */

public:
    GenIter(UniConfMountTree &root, const UniConfKey &key);
    ~GenIter();

    using UniConfMountTree::MountIter::split;
    using UniConfMountTree::MountIter::key;
    using UniConfMountTree::MountIter::head;
    using UniConfMountTree::MountIter::tail;
    using UniConfMountTree::MountIter::node;
    
    void rewind();
    bool next();

    UniConfGen *ptr() const
        { return genit ? genit->ptr() : NULL; }
    WvIterStuff(UniConfGen);
};


/** The UniConfMountTree implementation realized as a UniConfGen. */
class UniConfMountTreeGen : public UniConfGen
{
    UniConfMountTree *mounts;

    /** undefined. */
    UniConfMountTreeGen(const UniConfMountTreeGen &other);

public:
    /** Creates an empty UniConf tree with no mounted stores. */
    UniConfMountTreeGen();

    /** Destroys the UniConf tree along with all uncommitted data. */
    ~UniConfMountTreeGen();
    
    /**
     * Mounts a generator at a key using a moniker.
     * 
     * Returns the generator instance pointer, or NULL on failure.
     */
    virtual UniConfGen *mount(const UniConfKey &key, WvStringParm moniker,
        bool refresh);
    
    /**
     * Mounts a generator at a key.
     * Takes ownership of the supplied generator instance.
     * 
     * "key" is the key
     * "gen" is the generator instance
     * "refresh" is if true, refreshes the generator after mount
     * Returns: the generator instance pointer, or NULL on failure
     */
    virtual UniConfGen *mountgen(const UniConfKey &key, UniConfGen *gen,
        bool refresh);

    /**
     * Unmounts the generator at a key and destroys it.
     *
     * "key" is the key
     * "gen" is the generator instance
     * "commit" is if true, commits the generator before unmount
     */
    virtual void unmount(const UniConfKey &key, UniConfGen *gen, bool commit);
    
    /**
     * Finds the generator that owns a key.
     * 
     * If the key exists, returns the generator that provides its
     * contents.  Otherwise returns the generator that would be
     * updated if a value were set.
     * 
     * "key" is the key
     * "mountpoint" is if not NULL, replaced with the mountpoint
     *        path on success
     * Returns: the handle, or a null handle if none
     */
    virtual UniConfGen *whichmount(const UniConfKey &key, UniConfKey *mountpoint);

    /** Determines if a key is a mountpoint. */
    virtual bool ismountpoint(const UniConfKey &key);
    
    /***** Overridden members *****/
    
    friend class Iter : public UniConfAbstractIter
    {
        UniConfMountTreeGen *xroot;
        UniConfKey xkey;

        UniConfMountTree::GenIter genit;
        WvStringTable hack; // FIXME: ugly hack
        WvStringTable::Iter hackit;

    public:
        Iter(UniConfMountTreeGen &root, const UniConfKey &key);

        virtual void rewind();
        virtual bool next();
        
        virtual UniConfKey key() const;
    };
    
    virtual bool exists(const UniConfKey &key);
    virtual bool haschildren(const UniConfKey &key);
    virtual WvString get(const UniConfKey &key);
    virtual bool set(const UniConfKey &key, WvStringParm value);
    virtual bool zap(const UniConfKey &key);
    virtual bool refresh(const UniConfKey &key = UniConfKey::EMPTY,
        UniConfDepth::Type depth = UniConfDepth::INFINITE);
    virtual bool commit(const UniConfKey &key = UniConfKey::EMPTY,
        UniConfDepth::Type depth = UniConfDepth::INFINITE);
    virtual Iter *iterator(const UniConfKey &key);

private:
    /**
     * Prunes a branch of the tree beginning at the specified node
     * and moving towards the root.
     * "node" is the node
     */
    void prune(UniConfMountTree *node);

    typedef bool (*GenFunc)(UniConfGen*, const UniConfKey&,
        UniConfDepth::Type);
    bool dorecursive(GenFunc func,
        const UniConfKey &key, UniConfDepth::Type depth);
    bool dorecursivehelper(GenFunc func,
        UniConfMountTree *node, UniConfDepth::Type depth);

    static bool genrefreshfunc(UniConfGen *gen,
        const UniConfKey &key, UniConfDepth::Type depth);
    static bool gencommitfunc(UniConfGen *gen,
        const UniConfKey &key, UniConfDepth::Type depth);

    /** Called by generators when a key changes. */
    void gencallback(const UniConfGen &gen, const UniConfKey &key,
        UniConfDepth::Type depth, void *userdata);
};

#endif //__UNICONFMOUNTTREE_H
