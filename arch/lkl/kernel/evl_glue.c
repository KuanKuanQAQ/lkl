// SPDX-License-Identifier: GPL-2.0
#include <linux/string.h>
#include <linux/kstrtox.h>      /* simple_strtol */
#include <linux/stringhash.h>   /* hashlen_string */
#include <linux/rbtree.h>
#include <linux/find.h>
#include <linux/math.h>         /* int_sqrt */
#include <linux/time64.h>

int lkl_evl_strcmp(const char *a, const char *b)
{
        return strcmp(a, b);
}

void lkl_evl_rb_insert_color(struct rb_node *node, struct rb_root *root)
{
        rb_insert_color(node, root);
}

unsigned long lkl_evl_find_first_bit(const unsigned long *addr,
                                     unsigned long size)
{
        return find_first_bit(addr, size);
}

struct timespec64 lkl_evl_ns_to_timespec64(s64 nsec)
{
        return ns_to_timespec64(nsec);
}

int lkl_evl_strncmp(const char *a, const char *b, size_t count)
{
        return strncmp(a, b, count);
}

char *lkl_evl_strncpy(char *dest, const char *src, size_t count)
{
        return strncpy(dest, src, count);
}

char *lkl_evl_strsep(char **s, const char *ct)
{
        return strsep(s, ct);
}

long lkl_evl_simple_strtol(const char *cp, char **endp, unsigned int base)
{
        return simple_strtol(cp, endp, base);
}

u64 lkl_evl_hashlen_string(const void *salt, const char *name)
{
        return hashlen_string(salt, name);
}

void lkl_evl_rb_erase(struct rb_node *node, struct rb_root *root)
{
        rb_erase(node, root);
}

struct rb_node *lkl_evl_rb_next(const struct rb_node *node)
{
        return rb_next(node);
}

void lkl_evl_rb_replace_node(struct rb_node *victim, struct rb_node *new,
                             struct rb_root *root)
{
        rb_replace_node(victim, new, root);
}

unsigned long lkl_evl_find_first_zero_bit(const unsigned long *addr,
                                          unsigned long size)
{
        return find_first_zero_bit(addr, size);
}

unsigned long lkl_evl_int_sqrt(unsigned long x)
{
        return int_sqrt(x);
}
