/*
 * Copyright (c) 2005 William Pitcock, et al.
 * Rights to this code are as documented in doc/LICENSE.
 *
 * This file contains code for the nickserv DROP function.
 *
 */

#include "atheme.h"

DECLARE_MODULE_V1
(
	"nickserv/drop", false, _modinit, _moddeinit,
	PACKAGE_STRING,
	"Atheme Development Group <http://www.atheme.org>"
);

static void ns_cmd_drop(sourceinfo_t *si, int parc, char *parv[]);
static void ns_cmd_fdrop(sourceinfo_t *si, int parc, char *parv[]);

cmd_t ns_drop = { N_("Drops an account registration."), AC_NONE, "help/nickserv/drop", 2, ns_cmd_drop };
cmd_t ns_fdrop = { N_("Forces dropping an account registration."), AC_NONE, "help/nickserv/fdrop", 1, ns_cmd_fdrop };

list_t *ns_cmdtree, *ns_helptree;

void _modinit(module_t *m)
{
	MODULE_USE_SYMBOL(ns_cmdtree, "nickserv/main", "ns_cmdtree");
	MODULE_USE_SYMBOL(ns_helptree, "nickserv/main", "ns_helptree");

	cmd_add("nickserv:drop", &ns_drop);
	cmd_add("nickserv:fdrop", &ns_fdrop);
}

void _moddeinit()
{
	cmd_del("nickserv:drop");
	cmd_del("nickserv:fdrop");
}

static void ns_cmd_drop(sourceinfo_t *si, int parc, char *parv[])
{
	myuser_t *mu;
	mynick_t *mn;
	char *acc = parv[0];
	char *pass = parv[1];

	if (!acc || !pass)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "DROP");
		command_fail(si, fault_needmoreparams, _("Syntax: DROP <account> <password>"));
		return;
	}

	if (!(mu = myuser_find(acc)))
	{
		if (!nicksvs.no_nick_ownership)
		{
			mn = mynick_find(acc);
			if (mn != NULL && command_find(si->service->cmdtree, "UNGROUP"))
			{
				command_fail(si, fault_nosuch_target, _("\2%s\2 is a grouped nick, use %s to remove it."), acc, "UNGROUP");
				return;
			}
		}
		command_fail(si, fault_nosuch_target, _("\2%s\2 is not registered."), acc);
		return;
	}

	if (metadata_find(mu, "private:freeze:freezer"))
	{
		command_fail(si, fault_authfail, nicksvs.no_nick_ownership ? "You cannot login as \2%s\2 because the account has been frozen." : "You cannot identify to \2%s\2 because the nickname has been frozen.", mu->name);
		return;
	}

	if (!verify_password(mu, pass))
	{
		command_fail(si, fault_authfail, _("Authentication failed. Invalid password for \2%s\2."), mu->name);
		bad_password(si, mu);
		return;
	}

	if (!nicksvs.no_nick_ownership &&
			LIST_LENGTH(&mu->nicks) > 1 &&
			command_find(si->service->cmdtree, "UNGROUP"))
	{
		command_fail(si, fault_noprivs, _("Account \2%s\2 has %d other nick(s) grouped to it, remove those first."),
				mu->name, LIST_LENGTH(&mu->nicks) - 1);
		return;
	}

	if (is_soper(mu))
	{
		command_fail(si, fault_noprivs, _("The nickname \2%s\2 belongs to a services operator; it cannot be dropped."), acc);
		return;
	}

	if (mu->flags & MU_HOLD)
	{
		command_fail(si, fault_noprivs, _("The account \2%s\2 is held; it cannot be dropped."), acc);
		return;
	}

	command_add_flood(si, FLOOD_MODERATE);
	logcommand(si, CMDLOG_REGISTER, "DROP: \2%s\2", mu->name);
	hook_call_user_drop(mu);
	command_success_nodata(si, _("The account \2%s\2 has been dropped."), mu->name);
	object_unref(mu);
}

static void ns_cmd_fdrop(sourceinfo_t *si, int parc, char *parv[])
{
	myuser_t *mu;
	mynick_t *mn;
	char *acc = parv[0];

	if (!acc)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "FDROP");
		command_fail(si, fault_needmoreparams, _("Syntax: FDROP <account>"));
		return;
	}

	if (!(mu = myuser_find(acc)))
	{
		if (!nicksvs.no_nick_ownership)
		{
			mn = mynick_find(acc);
			if (mn != NULL && command_find(si->service->cmdtree, "FUNGROUP"))
			{
				command_fail(si, fault_nosuch_target, _("\2%s\2 is a grouped nick, use %s to remove it."), acc, "FUNGROUP");
				return;
			}
		}
		command_fail(si, fault_nosuch_target, _("\2%s\2 is not registered."), acc);
		return;
	}

	if (is_soper(mu))
	{
		command_fail(si, fault_noprivs, _("The nickname \2%s\2 belongs to a services operator; it cannot be dropped."), acc);
		return;
	}

	if (mu->flags & MU_HOLD)
	{
		command_fail(si, fault_noprivs, _("The account \2%s\2 is held; it cannot be dropped."), acc);
		return;
	}

	wallops("%s dropped the account \2%s\2", get_oper_name(si), mu->name);
	logcommand(si, CMDLOG_ADMIN | LG_REGISTER, "FDROP: \2%s\2", mu->name);
	hook_call_user_drop(mu);
	command_success_nodata(si, _("The account \2%s\2 has been dropped."), mu->name);
	object_unref(mu);
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */
