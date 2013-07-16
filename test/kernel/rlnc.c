#include <linux/module.h>
#include "linux/netlink.h"
#include "net/netlink.h"
#include <net/genetlink.h>

enum {
	BATADV_HLP_C_UNSPEC,
	BATADV_HLP_C_REGISTER,
	BATADV_HLP_C_GET_RELAYS,
	BATADV_HLP_C_GET_LINK,
	BATADV_HLP_C_GET_ONE_HOP,
	BATADV_HLP_C_FRAME,
	BATADV_HLP_C_BLOCK,
	BATADV_HLP_C_UNBLOCK,
	BATADV_HLP_C_NUM,
};
#define BATADV_HLP_C_MAX (BATADV_HLP_C_NUM - 1)

enum {
	BATADV_HLP_A_UNSPEC,
	BATADV_HLP_A_IFNAME,
	BATADV_HLP_A_IFINDEX,
	BATADV_HLP_A_SRC,
	BATADV_HLP_A_DST,
	BATADV_HLP_A_ADDR,
	BATADV_HLP_A_TQ,
	BATADV_HLP_A_HOP_LIST,
	BATADV_HLP_A_RLY_LIST,
	BATADV_HLP_A_FRAME,
	BATADV_HLP_A_BLOCK,
	BATADV_HLP_A_INT,
	BATADV_HLP_A_TYPE,
	BATADV_HLP_A_RANK,
	BATADV_HLP_A_SEQ,
	BATADV_HLP_A_ENCS,
	BATADV_HLP_A_E1,
	BATADV_HLP_A_E2,
	BATADV_HLP_A_E3,
	BATADV_HLP_A_NUM,
};
#define BATADV_HLP_A_MAX (BATADV_HLP_A_NUM - 1)

static int stub_port = 0;
static int rlnc_port = 0;

struct genl_family rlnc_genl_family = {
	.id = GENL_ID_GENERATE,
	.name = "batman_adv",
	.version = 1,
	.maxattr = BATADV_HLP_A_MAX,
};

int rlnc_genl_recv(struct sk_buff *skb, struct genl_info *info)
{
    struct sk_buff *skb_out = NULL;
    void *msg_head;
    u32 port, i;

    if (stub_port && !rlnc_port && info->snd_portid != stub_port) {
        rlnc_port = info->snd_portid;
        printk("registered rlnc port: %i\n", rlnc_port);
    } else if (!stub_port) {
        stub_port = info->snd_portid;
        printk("registered stub port: %i\n", stub_port);
    }

    if (!stub_port) {
        printk("waiting for stub port\n");
        return 0;
    }

    if(!rlnc_port) {
        printk("waiting for rlnc port\n");
        return 0;
    }

    port = info->snd_portid == stub_port ? rlnc_port : stub_port;

    skb_out = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
    if (!skb_out)
        return 0;

    msg_head = genlmsg_put(skb_out, 0, 0, &rlnc_genl_family, 0, info->genlhdr->cmd);

    for (i = 0; i < BATADV_HLP_A_NUM; i++) {
        if (!info->attrs[i])
            continue;

        printk("add attr: %i\n", i);

        if (nla_put(skb_out, i, nla_len(info->attrs[i]), nla_data(info->attrs[i])))
            printk("failed to put attr: %i\n", i);
    }

    genlmsg_end(skb_out, msg_head);
    genlmsg_unicast(genl_info_net(info), skb_out, port);

    printk("send genlmsg: %i\n", port);

    return 0;
}

static int rlnc_netlink_notify(struct notifier_block *nb,
			       unsigned long state, void *_notify)
{
    if (state != NETLINK_URELEASE)
        return NOTIFY_DONE;

    if (!stub_port && !rlnc_port)
        return NOTIFY_DONE;

    stub_port = 0;
    rlnc_port = 0;

    printk("netlink unregistered");

    return NOTIFY_DONE;
}


static struct genl_ops rlnc_genl_ops[] = {
	[BATADV_HLP_C_REGISTER - 1] = {
		.cmd = BATADV_HLP_C_REGISTER,
		.doit = rlnc_genl_recv,
	},
	[BATADV_HLP_C_GET_RELAYS - 1] = {
		.cmd = BATADV_HLP_C_GET_RELAYS,
		.doit = rlnc_genl_recv,
	},
	[BATADV_HLP_C_GET_LINK - 1] = {
		.cmd = BATADV_HLP_C_GET_LINK,
		.doit = rlnc_genl_recv,
	},
	[BATADV_HLP_C_GET_ONE_HOP - 1] = {
		.cmd = BATADV_HLP_C_GET_ONE_HOP,
		.doit = rlnc_genl_recv,
	},
	[BATADV_HLP_C_FRAME - 1] = {
		.cmd = BATADV_HLP_C_FRAME,
		.doit = rlnc_genl_recv,
	},
	[BATADV_HLP_C_BLOCK - 1] = {
		.cmd = BATADV_HLP_C_BLOCK,
		.doit = rlnc_genl_recv,
	},
	[BATADV_HLP_C_UNBLOCK - 1] = {
		.cmd = BATADV_HLP_C_UNBLOCK,
		.doit = rlnc_genl_recv,
	},
};

static struct notifier_block rlnc_netlink_notifier = {
        .notifier_call = rlnc_netlink_notify,
};

static int __init genl_init(void)
{
	genl_register_family_with_ops(&rlnc_genl_family, rlnc_genl_ops,
                                      ARRAY_SIZE(rlnc_genl_ops));
	netlink_register_notifier(&rlnc_netlink_notifier);

	return 0;
}

static void __exit genl_exit(void)
{
	genl_unregister_family(&rlnc_genl_family);
	netlink_unregister_notifier(&rlnc_netlink_notifier);
}

module_init(genl_init);
module_exit(genl_exit);

MODULE_LICENSE("GPL");
