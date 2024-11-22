#include <linux/jbd2.h>

static struct block_count *find_block_count(struct hot_blocks_track *hot_track, unsigned long long blocknr)
{
    struct rb_node *node = hot_track->root.rb_node;

    while (node) {
        struct block_count *bc = rb_entry(node, struct block_count, node);

        if (blocknr < bc->blocknr)
            node = node->rb_left;
        else if (blocknr > bc->blocknr)
            node = node->rb_right;
        else
            return bc;
    }
    return NULL;
}
static int insert_block_count(struct hot_blocks_track *hot_track, struct block_count *new_bc)
{
    struct rb_node **link = &hot_track->root.rb_node, *parent = NULL;

    while (*link) {
        struct block_count *bc = rb_entry(*link, struct block_count, node);

        parent = *link;
        if (new_bc->blocknr < bc->blocknr)
            link = &(*link)->rb_left;
        else if (new_bc->blocknr > bc->blocknr)
            link = &(*link)->rb_right;
        else
            return -EEXIST;
    }

    rb_link_node(&new_bc->node, parent, link);
    rb_insert_color(&new_bc->node, &hot_track->root);
    return 0;
}
/* Initialize hot blocks tracking */
int jbd2_init_hot_blocks(journal_t *journal)
{
    struct hot_blocks_track *hot_track;

    hot_track = kzalloc(sizeof(struct hot_blocks_track), GFP_KERNEL);
    if (!hot_track)
        return -ENOMEM;

    hot_track->root = RB_ROOT;
    spin_lock_init(&hot_track->lock);
    hot_track->window_size = JBD2_HOT_BLOCK_WINDOW;
    hot_track->hot_threshold = JBD2_HOT_THRESHOLD;

    journal->j_hot_track = hot_track;
    return 0;
}

/* Record block access in the tracking structure */
int jbd2_record_block_access(journal_t *journal, unsigned long long blocknr)
{
    struct hot_blocks_track *hot_track = journal->j_hot_track;
    struct block_count *bc;
    int ret = 0;

    if (!hot_track)
        return -EINVAL;

    spin_lock(&hot_track->lock);

    bc = find_block_count(hot_track, blocknr);
    if (bc) {
        bc->count++;
    } else {
        bc = kmalloc(sizeof(struct block_count), GFP_ATOMIC);
        if (!bc) {
            ret = -ENOMEM;
            goto out;
        }
        bc->blocknr = blocknr;
        bc->count = 1;
        ret = insert_block_count(hot_track, bc);
        if (ret) {
            kfree(bc);
            goto out;
        }
    }

out:
    spin_unlock(&hot_track->lock);
    return ret;
}
/* Print hot blocks during checkpoint */
void jbd2_print_hot_blocks(journal_t *journal)
{
    struct hot_blocks_track *hot_track = journal->j_hot_track;
    struct block_count *bc;
    struct rb_node *node;

    if (!hot_track)
        return;

    spin_lock(&hot_track->lock);
    
    printk(KERN_INFO "JBD2: Hot blocks in journal %s:\n", journal->j_devname);
    
    for (node = rb_first(&hot_track->root); node; node = rb_next(node)) {
        bc = rb_entry(node, struct block_count, node);
        if (bc->count >= hot_track->hot_threshold) {
            printk(KERN_INFO "  Block %llu: %u accesses\n",
                   bc->blocknr, bc->count);
        }
    }

    spin_unlock(&hot_track->lock);
}

/* Reset count for checkpointed block */
void jbd2_reset_all_block_counts(journal_t *journal)
{
    struct hot_blocks_track *hot_track = journal->j_hot_track;
    struct block_count *bc;
    struct rb_node *node;

    if (!hot_track)
        return;

    spin_lock(&hot_track->lock);

    for (node = rb_first(&hot_track->root); node; node = rb_next(node)) {
        bc = rb_entry(node, struct block_count, node);
        bc->count = 0;
    }

    spin_unlock(&hot_track->lock);
}

/* Cleanup function for hot blocks tracking */
void jbd2_cleanup_hot_blocks(journal_t *journal)
{
    struct hot_blocks_track *hot_track = journal->j_hot_track;
    struct block_count *bc;
    struct rb_node *node;

    if (!hot_track)
        return;

    for (node = rb_first(&hot_track->root); node; node = rb_next(node)) {
        bc = rb_entry(node, struct block_count, node);
        rb_erase(&bc->node, &hot_track->root);
        kfree(bc);
    }

    kfree(hot_track);
    journal->j_hot_track = NULL;
}
