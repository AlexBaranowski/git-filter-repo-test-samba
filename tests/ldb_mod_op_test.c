/*
 * from cmocka.c:
 * These headers or their equivalents should be included prior to
 * including
 * this header file.
 *
 * #include <stdarg.h>
 * #include <stddef.h>
 * #include <setjmp.h>
 *
 * This allows test applications to use custom definitions of C standard
 * library functions and types.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <errno.h>
#include <unistd.h>
#include <talloc.h>

#define TEVENT_DEPRECATED 1
#include <tevent.h>

#include <ldb.h>
#include <ldb_module.h>
#include <ldb_private.h>
#include <string.h>
#include <ctype.h>

#include <sys/wait.h>


#define DEFAULT_BE  "tdb"

#ifndef TEST_BE
#define TEST_BE DEFAULT_BE
#endif /* TEST_BE */

struct ldbtest_ctx {
	struct tevent_context *ev;
	struct ldb_context *ldb;

	const char *dbfile;
	const char *lockfile;   /* lockfile is separate */

	const char *dbpath;
};

static void unlink_old_db(struct ldbtest_ctx *test_ctx)
{
	int ret;

	errno = 0;
	ret = unlink(test_ctx->lockfile);
	if (ret == -1 && errno != ENOENT) {
		fail();
	}

	errno = 0;
	ret = unlink(test_ctx->dbfile);
	if (ret == -1 && errno != ENOENT) {
		fail();
	}
}

static int ldbtest_noconn_setup(void **state)
{
	struct ldbtest_ctx *test_ctx;

	test_ctx = talloc_zero(NULL, struct ldbtest_ctx);
	assert_non_null(test_ctx);

	test_ctx->ev = tevent_context_init(test_ctx);
	assert_non_null(test_ctx->ev);

	test_ctx->ldb = ldb_init(test_ctx, test_ctx->ev);
	assert_non_null(test_ctx->ldb);

	test_ctx->dbfile = talloc_strdup(test_ctx, "apitest.ldb");
	assert_non_null(test_ctx->dbfile);

	test_ctx->lockfile = talloc_asprintf(test_ctx, "%s-lock",
					     test_ctx->dbfile);
	assert_non_null(test_ctx->lockfile);

	test_ctx->dbpath = talloc_asprintf(test_ctx,
			TEST_BE"://%s", test_ctx->dbfile);
	assert_non_null(test_ctx->dbpath);

	unlink_old_db(test_ctx);
	*state = test_ctx;
	return 0;
}

static int ldbtest_noconn_teardown(void **state)
{
	struct ldbtest_ctx *test_ctx = talloc_get_type_abort(*state,
							struct ldbtest_ctx);

	unlink_old_db(test_ctx);
	talloc_free(test_ctx);
	return 0;
}

static void test_connect(void **state)
{
	struct ldbtest_ctx *test_ctx = talloc_get_type_abort(*state,
							struct ldbtest_ctx);
	int ret;

	ret = ldb_connect(test_ctx->ldb, test_ctx->dbpath, 0, NULL);
	assert_int_equal(ret, 0);
}

static int ldbtest_setup(void **state)
{
	struct ldbtest_ctx *test_ctx;
	int ret;

	ldbtest_noconn_setup((void **) &test_ctx);

	ret = ldb_connect(test_ctx->ldb, test_ctx->dbpath, 0, NULL);
	assert_int_equal(ret, 0);

	*state = test_ctx;
	return 0;
}

static int ldbtest_teardown(void **state)
{
	struct ldbtest_ctx *test_ctx = talloc_get_type_abort(*state,
							struct ldbtest_ctx);
	ldbtest_noconn_teardown((void **) &test_ctx);
	return 0;
}

static void test_ldb_add(void **state)
{
	int ret;
	struct ldb_message *msg;
	struct ldbtest_ctx *test_ctx = talloc_get_type_abort(*state,
							struct ldbtest_ctx);
	TALLOC_CTX *tmp_ctx;

	tmp_ctx = talloc_new(test_ctx);
	assert_non_null(tmp_ctx);

	msg = ldb_msg_new(tmp_ctx);
	assert_non_null(msg);

	msg->dn = ldb_dn_new_fmt(msg, test_ctx->ldb, "dc=test");
	assert_non_null(msg->dn);

	ret = ldb_msg_add_string(msg, "cn", "test_cn_val");
	assert_int_equal(ret, 0);

	ret = ldb_add(test_ctx->ldb, msg);
	assert_int_equal(ret, 0);

	talloc_free(tmp_ctx);
}

static void test_ldb_search(void **state)
{
	int ret;
	struct ldb_message *msg;
	struct ldbtest_ctx *test_ctx = talloc_get_type_abort(*state,
							struct ldbtest_ctx);
	TALLOC_CTX *tmp_ctx;
	struct ldb_dn *basedn;
	struct ldb_dn *basedn2;
	struct ldb_result *result = NULL;

	tmp_ctx = talloc_new(test_ctx);
	assert_non_null(tmp_ctx);

	basedn = ldb_dn_new_fmt(tmp_ctx, test_ctx->ldb, "dc=test");
	assert_non_null(basedn);

	ret = ldb_search(test_ctx->ldb, tmp_ctx, &result, basedn,
			 LDB_SCOPE_BASE, NULL, NULL);
	assert_int_equal(ret, 0);
	assert_non_null(result);
	assert_int_equal(result->count, 0);

	msg = ldb_msg_new(tmp_ctx);
	assert_non_null(msg);

	msg->dn = basedn;
	assert_non_null(msg->dn);

	ret = ldb_msg_add_string(msg, "cn", "test_cn_val1");
	assert_int_equal(ret, 0);

	ret = ldb_add(test_ctx->ldb, msg);
	assert_int_equal(ret, 0);

	basedn2 = ldb_dn_new_fmt(tmp_ctx, test_ctx->ldb, "dc=test2");
	assert_non_null(basedn2);

	msg = ldb_msg_new(tmp_ctx);
	assert_non_null(msg);

	msg->dn = basedn2;
	assert_non_null(msg->dn);

	ret = ldb_msg_add_string(msg, "cn", "test_cn_val2");
	assert_int_equal(ret, 0);

	ret = ldb_add(test_ctx->ldb, msg);
	assert_int_equal(ret, 0);

	ret = ldb_search(test_ctx->ldb, tmp_ctx, &result, basedn,
			 LDB_SCOPE_BASE, NULL, NULL);
	assert_int_equal(ret, 0);
	assert_non_null(result);
	assert_int_equal(result->count, 1);
	assert_string_equal(ldb_dn_get_linearized(result->msgs[0]->dn),
			    ldb_dn_get_linearized(basedn));

	ret = ldb_search(test_ctx->ldb, tmp_ctx, &result, basedn2,
			 LDB_SCOPE_BASE, NULL, NULL);
	assert_int_equal(ret, 0);
	assert_non_null(result);
	assert_int_equal(result->count, 1);
	assert_string_equal(ldb_dn_get_linearized(result->msgs[0]->dn),
			    ldb_dn_get_linearized(basedn2));

	talloc_free(tmp_ctx);
}

static int base_search_count(struct ldbtest_ctx *test_ctx, const char *entry_dn)
{
	TALLOC_CTX *tmp_ctx;
	struct ldb_dn *basedn;
	struct ldb_result *result = NULL;
	int ret;
	int count;

	tmp_ctx = talloc_new(test_ctx);
	assert_non_null(tmp_ctx);

	basedn = ldb_dn_new_fmt(tmp_ctx, test_ctx->ldb, "%s", entry_dn);
	assert_non_null(basedn);

	ret = ldb_search(test_ctx->ldb, tmp_ctx, &result, basedn,
			 LDB_SCOPE_BASE, NULL, NULL);
	assert_int_equal(ret, LDB_SUCCESS);
	assert_non_null(result);

	count = result->count;
	talloc_free(tmp_ctx);
	return count;
}

static int sub_search_count(struct ldbtest_ctx *test_ctx,
			    const char *base_dn,
			    const char *filter)
{
	TALLOC_CTX *tmp_ctx;
	struct ldb_dn *basedn;
	struct ldb_result *result = NULL;
	int ret;
	int count;

	tmp_ctx = talloc_new(test_ctx);
	assert_non_null(tmp_ctx);

	basedn = ldb_dn_new_fmt(tmp_ctx, test_ctx->ldb, "%s", base_dn);
	assert_non_null(basedn);

	ret = ldb_search(test_ctx->ldb, tmp_ctx, &result, basedn,
			 LDB_SCOPE_SUBTREE, NULL, "%s", filter);
	assert_int_equal(ret, LDB_SUCCESS);
	assert_non_null(result);

	count = result->count;
	talloc_free(tmp_ctx);
	return count;
}

/* In general it would be better if utility test functions didn't assert
 * but only returned a value, then assert in the test shows correct
 * line
 */
static void assert_dn_exists(struct ldbtest_ctx *test_ctx,
			     const char *entry_dn)
{
	int count;

	count = base_search_count(test_ctx, entry_dn);
	assert_int_equal(count, 1);
}

static void assert_dn_doesnt_exist(struct ldbtest_ctx *test_ctx,
				   const char *entry_dn)
{
	int count;

	count = base_search_count(test_ctx, entry_dn);
	assert_int_equal(count, 0);
}

static void add_dn_with_cn(struct ldbtest_ctx *test_ctx,
			   struct ldb_dn *dn,
			   const char *cn_value)
{
	int ret;
	TALLOC_CTX *tmp_ctx;
	struct ldb_message *msg;

	tmp_ctx = talloc_new(test_ctx);
	assert_non_null(tmp_ctx);

	assert_dn_doesnt_exist(test_ctx,
			       ldb_dn_get_linearized(dn));

	msg = ldb_msg_new(tmp_ctx);
	assert_non_null(msg);
	msg->dn = dn;

	ret = ldb_msg_add_string(msg, "cn", cn_value);
	assert_int_equal(ret, LDB_SUCCESS);

	ret = ldb_add(test_ctx->ldb, msg);
	assert_int_equal(ret, LDB_SUCCESS);

	assert_dn_exists(test_ctx,
			 ldb_dn_get_linearized(dn));
	talloc_free(tmp_ctx);
}

static void test_ldb_del(void **state)
{
	int ret;
	struct ldbtest_ctx *test_ctx = talloc_get_type_abort(*state,
							struct ldbtest_ctx);
	const char *basedn = "dc=ldb_del_test";
	struct ldb_dn *dn;

	dn = ldb_dn_new_fmt(test_ctx, test_ctx->ldb, "%s", basedn);
	assert_non_null(dn);

	add_dn_with_cn(test_ctx, dn, "test_del_cn_val");

	ret = ldb_delete(test_ctx->ldb, dn);
	assert_int_equal(ret, LDB_SUCCESS);

	assert_dn_doesnt_exist(test_ctx, basedn);
}

static void test_ldb_del_noexist(void **state)
{
	struct ldbtest_ctx *test_ctx = talloc_get_type_abort(*state,
							     struct ldbtest_ctx);
	struct ldb_dn *basedn;
	int ret;

	basedn = ldb_dn_new(test_ctx, test_ctx->ldb, "dc=nosuchplace");
	assert_non_null(basedn);

	ret = ldb_delete(test_ctx->ldb, basedn);
	assert_int_equal(ret, LDB_ERR_NO_SUCH_OBJECT);
}

static void test_ldb_handle(void **state)
{
	int ret;
	struct ldbtest_ctx *test_ctx = talloc_get_type_abort(*state,
							struct ldbtest_ctx);
	TALLOC_CTX *tmp_ctx;
	struct ldb_dn *basedn;
	struct ldb_request *request = NULL;
	struct ldb_request *request2 = NULL;
	struct ldb_result *res = NULL;
	const char *attrs[] = { "cn", NULL };

	tmp_ctx = talloc_new(test_ctx);
	assert_non_null(tmp_ctx);

	basedn = ldb_dn_new_fmt(tmp_ctx, test_ctx->ldb, "dc=test");
	assert_non_null(basedn);

	res = talloc_zero(tmp_ctx, struct ldb_result);
	assert_non_null(res);

	ret = ldb_build_search_req(&request, test_ctx->ldb, tmp_ctx,
				   basedn, LDB_SCOPE_BASE,
				   NULL, attrs, NULL, res,
				   ldb_search_default_callback,
				   NULL);
	assert_int_equal(ret, 0);

	/* We are against ldb_tdb, so expect private event contexts */
	assert_ptr_not_equal(ldb_handle_get_event_context(request->handle),
			     ldb_get_event_context(test_ctx->ldb));

	ret = ldb_build_search_req(&request2, test_ctx->ldb, tmp_ctx,
				   basedn, LDB_SCOPE_BASE,
				   NULL, attrs, NULL, res,
				   ldb_search_default_callback,
				   request);
	assert_int_equal(ret, 0);

	/* Expect that same event context will be chained */
	assert_ptr_equal(ldb_handle_get_event_context(request->handle),
			 ldb_handle_get_event_context(request2->handle));

	/* Now force this to use the global context */
	ldb_handle_use_global_event_context(request2->handle);
	assert_ptr_equal(ldb_handle_get_event_context(request2->handle),
			 ldb_get_event_context(test_ctx->ldb));

	talloc_free(tmp_ctx);
}

static void test_ldb_build_search_req(void **state)
{
	int ret;
	struct ldbtest_ctx *test_ctx = talloc_get_type_abort(*state,
							struct ldbtest_ctx);
	TALLOC_CTX *tmp_ctx;
	struct ldb_dn *basedn;
	struct ldb_request *request = NULL;
	struct ldb_request *request2 = NULL;
	struct ldb_result *res = NULL;
	const char *attrs[] = { "cn", NULL };

	tmp_ctx = talloc_new(test_ctx);
	assert_non_null(tmp_ctx);

	basedn = ldb_dn_new_fmt(tmp_ctx, test_ctx->ldb, "dc=test");
	assert_non_null(basedn);

	res = talloc_zero(tmp_ctx, struct ldb_result);
	assert_non_null(res);

	ret = ldb_build_search_req(&request, test_ctx->ldb, tmp_ctx,
				   basedn, LDB_SCOPE_BASE,
				   NULL, attrs, NULL, res,
				   ldb_search_default_callback,
				   NULL);
	assert_int_equal(ret, 0);

	assert_int_equal(request->operation, LDB_SEARCH);
	assert_ptr_equal(request->op.search.base, basedn);
	assert_int_equal(request->op.search.scope, LDB_SCOPE_BASE);
	assert_non_null(request->op.search.tree);
	assert_ptr_equal(request->op.search.attrs, attrs);
	assert_ptr_equal(request->context, res);
	assert_ptr_equal(request->callback, ldb_search_default_callback);

	ret = ldb_build_search_req(&request2, test_ctx->ldb, tmp_ctx,
				   basedn, LDB_SCOPE_BASE,
				   NULL, attrs, NULL, res,
				   ldb_search_default_callback,
				   request);
	assert_int_equal(ret, 0);
	assert_ptr_equal(request, request2->handle->parent);
	assert_int_equal(request->starttime, request2->starttime);
	assert_int_equal(request->timeout, request2->timeout);

	talloc_free(tmp_ctx);
}

static void add_keyval(struct ldbtest_ctx *test_ctx,
		       const char *key,
		       const char *val)
{
	int ret;
	struct ldb_message *msg;

	msg = ldb_msg_new(test_ctx);
	assert_non_null(msg);

	msg->dn = ldb_dn_new_fmt(msg, test_ctx->ldb, "%s=%s", key, val);
	assert_non_null(msg->dn);

	ret = ldb_msg_add_string(msg, key, val);
	assert_int_equal(ret, 0);

	ret = ldb_add(test_ctx->ldb, msg);
	assert_int_equal(ret, 0);

	talloc_free(msg);
}

static struct ldb_result *get_keyval(struct ldbtest_ctx *test_ctx,
				     const char *key,
				     const char *val)
{
	int ret;
	struct ldb_result *result;
	struct ldb_dn *basedn;

	basedn = ldb_dn_new_fmt(test_ctx, test_ctx->ldb, "%s=%s", key, val);
	assert_non_null(basedn);

	ret = ldb_search(test_ctx->ldb, test_ctx, &result, basedn,
			LDB_SCOPE_BASE, NULL, NULL);
	assert_int_equal(ret, 0);

	return result;
}

static void test_transactions(void **state)
{
	int ret;
	struct ldbtest_ctx *test_ctx = talloc_get_type_abort(*state,
			struct ldbtest_ctx);
	struct ldb_result *res;

	/* start lev-0 transaction */
	ret = ldb_transaction_start(test_ctx->ldb);
	assert_int_equal(ret, 0);

	add_keyval(test_ctx, "vegetable", "carrot");

	/* commit lev-0 transaction */
	ret = ldb_transaction_commit(test_ctx->ldb);
	assert_int_equal(ret, 0);

	/* start another lev-1 nested transaction */
	ret = ldb_transaction_start(test_ctx->ldb);
	assert_int_equal(ret, 0);

	add_keyval(test_ctx, "fruit", "apple");

	/* abort lev-1 nested transaction */
	ret = ldb_transaction_cancel(test_ctx->ldb);
	assert_int_equal(ret, 0);

	res = get_keyval(test_ctx, "vegetable", "carrot");
	assert_non_null(res);
	assert_int_equal(res->count, 1);

	res = get_keyval(test_ctx, "fruit", "apple");
	assert_non_null(res);
	assert_int_equal(res->count, 0);
}

struct ldb_mod_test_ctx {
	struct ldbtest_ctx *ldb_test_ctx;
	const char *entry_dn;
};

struct keyval {
	const char *key;
	const char *val;
};

static struct ldb_message *build_mod_msg(TALLOC_CTX *mem_ctx,
					 struct ldbtest_ctx *test_ctx,
					 const char *dn,
					 int modify_flags,
					 struct keyval *kvs)
{
	struct ldb_message *msg;
	int ret;
	int i;

	msg = ldb_msg_new(mem_ctx);
	assert_non_null(msg);

	msg->dn = ldb_dn_new_fmt(msg, test_ctx->ldb, "%s", dn);
	assert_non_null(msg->dn);

	for (i = 0; kvs[i].key != NULL; i++) {
		if (modify_flags) {
			ret = ldb_msg_add_empty(msg, kvs[i].key,
						modify_flags, NULL);
			assert_int_equal(ret, 0);
		}

		if (kvs[i].val) {
			ret = ldb_msg_add_string(msg, kvs[i].key, kvs[i].val);
			assert_int_equal(ret, LDB_SUCCESS);
		}
	}

	return msg;
}

static void ldb_test_add_data(TALLOC_CTX *mem_ctx,
			      struct ldbtest_ctx *ldb_test_ctx,
			      const char *basedn,
			      struct keyval *kvs)
{
	TALLOC_CTX *tmp_ctx;
	struct ldb_message *msg;
	struct ldb_result *result = NULL;
	int ret;

	tmp_ctx = talloc_new(mem_ctx);
	assert_non_null(tmp_ctx);

	msg = build_mod_msg(tmp_ctx, ldb_test_ctx,
			    basedn, 0, kvs);
	assert_non_null(msg);

	ret = ldb_add(ldb_test_ctx->ldb, msg);
	assert_int_equal(ret, LDB_SUCCESS);

	ret = ldb_search(ldb_test_ctx->ldb, tmp_ctx, &result, msg->dn,
			 LDB_SCOPE_BASE, NULL, NULL);
	assert_int_equal(ret, LDB_SUCCESS);
	assert_non_null(result);
	assert_int_equal(result->count, 1);
	assert_string_equal(ldb_dn_get_linearized(result->msgs[0]->dn),
			    ldb_dn_get_linearized(msg->dn));

	talloc_free(tmp_ctx);
}

static void ldb_test_remove_data(TALLOC_CTX *mem_ctx,
				 struct ldbtest_ctx *ldb_test_ctx,
				 const char *strdn)
{
	TALLOC_CTX *tmp_ctx;
	struct ldb_dn *basedn;
	int ret;
	size_t count;

	tmp_ctx = talloc_new(mem_ctx);
	assert_non_null(tmp_ctx);

	basedn = ldb_dn_new_fmt(tmp_ctx, ldb_test_ctx->ldb,
				"%s", strdn);
	assert_non_null(basedn);

	ret = ldb_delete(ldb_test_ctx->ldb, basedn);
	assert_true(ret == LDB_SUCCESS || ret == LDB_ERR_NO_SUCH_OBJECT);

	count = base_search_count(ldb_test_ctx, ldb_dn_get_linearized(basedn));
	assert_int_equal(count, 0);

	talloc_free(tmp_ctx);
}

static void mod_test_add_data(struct ldb_mod_test_ctx *mod_test_ctx,
			      struct keyval *kvs)
{
	ldb_test_add_data(mod_test_ctx,
			  mod_test_ctx->ldb_test_ctx,
			  mod_test_ctx->entry_dn,
			  kvs);
}

static void mod_test_remove_data(struct ldb_mod_test_ctx *mod_test_ctx)
{
	ldb_test_remove_data(mod_test_ctx,
			     mod_test_ctx->ldb_test_ctx,
			     mod_test_ctx->entry_dn);
}

static struct ldb_result *run_mod_test(struct ldb_mod_test_ctx *mod_test_ctx,
				       int modify_flags,
				       struct keyval *kvs)
{
	TALLOC_CTX *tmp_ctx;
	struct ldb_result *res;
	struct ldb_message *mod_msg;
	struct ldb_dn *basedn;
	struct ldbtest_ctx *ldb_test_ctx;
	int ret;

	ldb_test_ctx = mod_test_ctx->ldb_test_ctx;

	tmp_ctx = talloc_new(mod_test_ctx);
	assert_non_null(tmp_ctx);

	mod_msg = build_mod_msg(tmp_ctx, ldb_test_ctx, mod_test_ctx->entry_dn,
				modify_flags, kvs);
	assert_non_null(mod_msg);

	ret = ldb_modify(ldb_test_ctx->ldb, mod_msg);
	assert_int_equal(ret, LDB_SUCCESS);

	basedn = ldb_dn_new_fmt(tmp_ctx, ldb_test_ctx->ldb,
			"%s", mod_test_ctx->entry_dn);
	assert_non_null(basedn);

	ret = ldb_search(ldb_test_ctx->ldb, mod_test_ctx, &res, basedn,
			 LDB_SCOPE_BASE, NULL, NULL);
	assert_int_equal(ret, LDB_SUCCESS);
	assert_non_null(res);
	assert_int_equal(res->count, 1);
	assert_string_equal(ldb_dn_get_linearized(res->msgs[0]->dn),
			    ldb_dn_get_linearized(mod_msg->dn));

	talloc_free(tmp_ctx);
	return res;
}

static int ldb_modify_test_setup(void **state)
{
	struct ldbtest_ctx *ldb_test_ctx;
	struct ldb_mod_test_ctx *mod_test_ctx;
	struct keyval kvs[] = {
		{ "cn", "test_mod_cn" },
		{ NULL, NULL },
	};

	ldbtest_setup((void **) &ldb_test_ctx);

	mod_test_ctx = talloc(ldb_test_ctx, struct ldb_mod_test_ctx);
	assert_non_null(mod_test_ctx);

	mod_test_ctx->entry_dn = "dc=mod_test_entry";
	mod_test_ctx->ldb_test_ctx = ldb_test_ctx;

	mod_test_remove_data(mod_test_ctx);
	mod_test_add_data(mod_test_ctx, kvs);
	*state = mod_test_ctx;
	return 0;
}

static int ldb_modify_test_teardown(void **state)
{
	struct ldb_mod_test_ctx *mod_test_ctx = \
				talloc_get_type_abort(*state,
						      struct ldb_mod_test_ctx);
	struct ldbtest_ctx *ldb_test_ctx;

	ldb_test_ctx = mod_test_ctx->ldb_test_ctx;

	mod_test_remove_data(mod_test_ctx);
	talloc_free(mod_test_ctx);

	ldbtest_teardown((void **) &ldb_test_ctx);
	return 0;
}

static void test_ldb_modify_add_key(void **state)
{
	struct ldb_mod_test_ctx *mod_test_ctx = \
				talloc_get_type_abort(*state,
						      struct ldb_mod_test_ctx);
	struct keyval mod_kvs[] = {
		{ "name", "test_mod_name" },
		{ NULL, NULL },
	};
	struct ldb_result *res;
	struct ldb_message_element *el;

	res = run_mod_test(mod_test_ctx, LDB_FLAG_MOD_ADD, mod_kvs);
	assert_non_null(res);

	/* Check cn is intact and name was added */
	assert_int_equal(res->count, 1);
	el = ldb_msg_find_element(res->msgs[0], "cn");
	assert_non_null(el);
	assert_int_equal(el->num_values, 1);
	assert_string_equal(el->values[0].data, "test_mod_cn");

	el = ldb_msg_find_element(res->msgs[0], "name");
	assert_non_null(el);
	assert_int_equal(el->num_values, 1);
	assert_string_equal(el->values[0].data, "test_mod_name");
}

static void test_ldb_modify_extend_key(void **state)
{
	struct ldb_mod_test_ctx *mod_test_ctx = \
			talloc_get_type_abort(*state,
					      struct ldb_mod_test_ctx);
	struct keyval mod_kvs[] = {
		{ "cn", "test_mod_cn2" },
		{ NULL, NULL },
	};
	struct ldb_result *res;
	struct ldb_message_element *el;

	res = run_mod_test(mod_test_ctx, LDB_FLAG_MOD_ADD, mod_kvs);
	assert_non_null(res);

	/* Check cn was extended with another value */
	assert_int_equal(res->count, 1);
	el = ldb_msg_find_element(res->msgs[0], "cn");
	assert_non_null(el);
	assert_int_equal(el->num_values, 2);
	assert_string_equal(el->values[0].data, "test_mod_cn");
	assert_string_equal(el->values[1].data, "test_mod_cn2");
}

static void test_ldb_modify_add_key_noval(void **state)
{
	struct ldb_mod_test_ctx *mod_test_ctx = \
			talloc_get_type_abort(*state,
					      struct ldb_mod_test_ctx);
	struct ldb_message *mod_msg;
	struct ldbtest_ctx *ldb_test_ctx;
	struct ldb_message_element *el;
	int ret;

	ldb_test_ctx = mod_test_ctx->ldb_test_ctx;

	mod_msg = ldb_msg_new(mod_test_ctx);
	assert_non_null(mod_msg);

	mod_msg->dn = ldb_dn_new_fmt(mod_msg, ldb_test_ctx->ldb,
			"%s", mod_test_ctx->entry_dn);
	assert_non_null(mod_msg->dn);

	el = talloc_zero(mod_msg, struct ldb_message_element);
	el->flags = LDB_FLAG_MOD_ADD;
	assert_non_null(el);
	el->name = talloc_strdup(el, "cn");
	assert_non_null(el->name);

	mod_msg->elements = el;
	mod_msg->num_elements = 1;

	ret = ldb_modify(ldb_test_ctx->ldb, mod_msg);
	assert_int_equal(ret, LDB_ERR_CONSTRAINT_VIOLATION);
}

static void test_ldb_modify_replace_key(void **state)
{
	struct ldb_mod_test_ctx *mod_test_ctx = \
			talloc_get_type_abort(*state,
					      struct ldb_mod_test_ctx);
	const char *new_cn = "new_cn";
	struct keyval mod_kvs[] = {
		{ "cn", new_cn },
		{ NULL, NULL },
	};
	struct ldb_result *res;
	struct ldb_message_element *el;

	res = run_mod_test(mod_test_ctx, LDB_FLAG_MOD_REPLACE, mod_kvs);
	assert_non_null(res);

	/* Check cn was replaced */
	assert_int_equal(res->count, 1);
	el = ldb_msg_find_element(res->msgs[0], "cn");
	assert_non_null(el);
	assert_int_equal(el->num_values, 1);
	assert_string_equal(el->values[0].data, new_cn);
}

static void test_ldb_modify_replace_noexist_key(void **state)
{
	struct ldb_mod_test_ctx *mod_test_ctx = \
			talloc_get_type_abort(*state,
					      struct ldb_mod_test_ctx);
	struct keyval mod_kvs[] = {
		{ "name", "name_val" },
		{ NULL, NULL },
	};
	struct ldb_result *res;
	struct ldb_message_element *el;

	res = run_mod_test(mod_test_ctx, LDB_FLAG_MOD_REPLACE, mod_kvs);
	assert_non_null(res);

	/* Check cn is intact and name was added */
	assert_int_equal(res->count, 1);
	el = ldb_msg_find_element(res->msgs[0], "cn");
	assert_non_null(el);
	assert_int_equal(el->num_values, 1);
	assert_string_equal(el->values[0].data, "test_mod_cn");

	el = ldb_msg_find_element(res->msgs[0], mod_kvs[0].key);
	assert_non_null(el);
	assert_int_equal(el->num_values, 1);
	assert_string_equal(el->values[0].data, mod_kvs[0].val);
}

static void test_ldb_modify_replace_zero_vals(void **state)
{
	struct ldb_mod_test_ctx *mod_test_ctx = \
			talloc_get_type_abort(*state,
					      struct ldb_mod_test_ctx);
	struct ldb_message_element *el;
	struct ldb_result *res;
	struct keyval kvs[] = {
		{ "cn", NULL },
		{ NULL, NULL },
	};

	/* cn must be gone */
	res = run_mod_test(mod_test_ctx, LDB_FLAG_MOD_REPLACE, kvs);
	assert_non_null(res);
	el = ldb_msg_find_element(res->msgs[0], "cn");
	assert_null(el);
}

static void test_ldb_modify_replace_noexist_key_zero_vals(void **state)
{
	struct ldb_mod_test_ctx *mod_test_ctx = \
			talloc_get_type_abort(*state,
					      struct ldb_mod_test_ctx);
	struct ldb_message_element *el;
	struct ldb_result *res;
	struct keyval kvs[] = {
		{ "noexist_key", NULL },
		{ NULL, NULL },
	};

	/* cn must be gone */
	res = run_mod_test(mod_test_ctx, LDB_FLAG_MOD_REPLACE, kvs);
	assert_non_null(res);

	/* cn should be intact */
	el = ldb_msg_find_element(res->msgs[0], "cn");
	assert_non_null(el);
}

static void test_ldb_modify_del_key(void **state)
{
	struct ldb_mod_test_ctx *mod_test_ctx = \
			talloc_get_type_abort(*state,
					      struct ldb_mod_test_ctx);
	struct ldb_message_element *el;
	struct ldb_result *res;
	struct keyval kvs[] = {
		{ "cn", NULL },
		{ NULL, NULL },
	};

	/* cn must be gone */
	res = run_mod_test(mod_test_ctx, LDB_FLAG_MOD_DELETE, kvs);
	assert_non_null(res);

	el = ldb_msg_find_element(res->msgs[0], "cn");
	assert_null(el);
}

static void test_ldb_modify_del_keyval(void **state)
{
	struct ldb_mod_test_ctx *mod_test_ctx = \
			talloc_get_type_abort(*state,
					      struct ldb_mod_test_ctx);
	struct ldb_message_element *el;
	struct ldb_result *res;
	struct keyval kvs[] = {
		{ "cn", "test_mod_cn" },
		{ NULL, NULL },
	};

	/* cn must be gone */
	res = run_mod_test(mod_test_ctx, LDB_FLAG_MOD_DELETE, kvs);
	assert_non_null(res);

	el = ldb_msg_find_element(res->msgs[0], "cn");
	assert_null(el);
}

struct search_test_ctx {
	struct ldbtest_ctx *ldb_test_ctx;
	const char *base_dn;
};

static char *get_full_dn(TALLOC_CTX *mem_ctx,
			 struct search_test_ctx *search_test_ctx,
			 const char *rdn)
{
	char *full_dn;

	full_dn = talloc_asprintf(mem_ctx,
				  "%s,%s", rdn, search_test_ctx->base_dn);
	assert_non_null(full_dn);

	return full_dn;
}

static void search_test_add_data(struct search_test_ctx *search_test_ctx,
				 const char *rdn,
				 struct keyval *kvs)
{
	char *full_dn;

	full_dn = get_full_dn(search_test_ctx, search_test_ctx, rdn);

	ldb_test_add_data(search_test_ctx,
			  search_test_ctx->ldb_test_ctx,
			  full_dn,
			  kvs);
}

static void search_test_remove_data(struct search_test_ctx *search_test_ctx,
				    const char *rdn)
{
	char *full_dn;

	full_dn = talloc_asprintf(search_test_ctx,
				  "%s,%s", rdn, search_test_ctx->base_dn);
	assert_non_null(full_dn);

	ldb_test_remove_data(search_test_ctx,
			     search_test_ctx->ldb_test_ctx,
			     full_dn);
}

static int ldb_search_test_setup(void **state)
{
	struct ldbtest_ctx *ldb_test_ctx;
	struct search_test_ctx *search_test_ctx;
	struct keyval kvs[] = {
		{ "cn", "test_search_cn" },
		{ "cn", "test_search_cn2" },
		{ "uid", "test_search_uid" },
		{ "uid", "test_search_uid2" },
		{ NULL, NULL },
	};
	struct keyval kvs2[] = {
		{ "cn", "test_search_2_cn" },
		{ "cn", "test_search_2_cn2" },
		{ "uid", "test_search_2_uid" },
		{ "uid", "test_search_2_uid2" },
		{ NULL, NULL },
	};

	ldbtest_setup((void **) &ldb_test_ctx);

	search_test_ctx = talloc(ldb_test_ctx, struct search_test_ctx);
	assert_non_null(search_test_ctx);

	search_test_ctx->base_dn = "dc=search_test_entry";
	search_test_ctx->ldb_test_ctx = ldb_test_ctx;

	search_test_remove_data(search_test_ctx, "cn=test_search_cn");
	search_test_add_data(search_test_ctx, "cn=test_search_cn", kvs);

	search_test_remove_data(search_test_ctx, "cn=test_search_2_cn");
	search_test_add_data(search_test_ctx, "cn=test_search_2_cn", kvs2);

	*state = search_test_ctx;
	return 0;
}

static int ldb_search_test_teardown(void **state)
{
	struct search_test_ctx *search_test_ctx = talloc_get_type_abort(*state,
			struct search_test_ctx);
	struct ldbtest_ctx *ldb_test_ctx;

	ldb_test_ctx = search_test_ctx->ldb_test_ctx;

	search_test_remove_data(search_test_ctx, "cn=test_search_cn");
	search_test_remove_data(search_test_ctx, "cn=test_search_2_cn");
	ldbtest_teardown((void **) &ldb_test_ctx);
	return 0;
}

static void assert_attr_has_vals(struct ldb_message *msg,
				 const char *attr,
				 const char *vals[],
				 const size_t nvals)
{
	struct ldb_message_element *el;
	size_t i;

	el = ldb_msg_find_element(msg, attr);
	assert_non_null(el);

	assert_int_equal(el->num_values, nvals);
	for (i = 0; i < nvals; i++) {
		assert_string_equal(el->values[i].data,
				    vals[i]);
	}
}

static void assert_has_no_attr(struct ldb_message *msg,
			       const char *attr)
{
	struct ldb_message_element *el;

	el = ldb_msg_find_element(msg, attr);
	assert_null(el);
}

static bool has_dn(struct ldb_message *msg, const char *dn)
{
	const char *msgdn;

	msgdn = ldb_dn_get_linearized(msg->dn);
	if (strcmp(dn, msgdn) == 0) {
		return true;
	}

	return false;
}

static void test_search_match_none(void **state)
{
	struct search_test_ctx *search_test_ctx = talloc_get_type_abort(*state,
			struct search_test_ctx);
	size_t count;

	count = base_search_count(search_test_ctx->ldb_test_ctx,
				  "dc=no_such_entry");
	assert_int_equal(count, 0);
}

static void test_search_match_one(void **state)
{
	struct search_test_ctx *search_test_ctx = talloc_get_type_abort(*state,
			struct search_test_ctx);
	int ret;
	struct ldb_dn *basedn;
	struct ldb_result *result = NULL;
	const char *cn_vals[] = { "test_search_cn",
				  "test_search_cn2" };
	const char *uid_vals[] = { "test_search_uid",
				   "test_search_uid2" };

	basedn = ldb_dn_new_fmt(search_test_ctx,
				search_test_ctx->ldb_test_ctx->ldb,
				"%s",
				search_test_ctx->base_dn);
	assert_non_null(basedn);

	ret = ldb_search(search_test_ctx->ldb_test_ctx->ldb,
			 search_test_ctx,
			 &result,
			 basedn,
			 LDB_SCOPE_SUBTREE, NULL,
			 "cn=test_search_cn");
	assert_int_equal(ret, 0);
	assert_non_null(result);
	assert_int_equal(result->count, 1);

	assert_attr_has_vals(result->msgs[0], "cn", cn_vals, 2);
	assert_attr_has_vals(result->msgs[0], "uid", uid_vals, 2);
}

static void test_search_match_filter(void **state)
{
	struct search_test_ctx *search_test_ctx = talloc_get_type_abort(*state,
			struct search_test_ctx);
	int ret;
	struct ldb_dn *basedn;
	struct ldb_result *result = NULL;
	const char *cn_vals[] = { "test_search_cn",
				  "test_search_cn2" };
	const char *attrs[] = { "cn", NULL };

	basedn = ldb_dn_new_fmt(search_test_ctx,
			        search_test_ctx->ldb_test_ctx->ldb,
				"%s",
				search_test_ctx->base_dn);
	assert_non_null(basedn);

	ret = ldb_search(search_test_ctx->ldb_test_ctx->ldb,
			 search_test_ctx,
			 &result,
			 basedn,
			 LDB_SCOPE_SUBTREE,
			 attrs,
			 "cn=test_search_cn");
	assert_int_equal(ret, 0);
	assert_non_null(result);
	assert_int_equal(result->count, 1);

	assert_attr_has_vals(result->msgs[0], "cn", cn_vals, 2);
	assert_has_no_attr(result->msgs[0], "uid");
}

static void assert_expected(struct search_test_ctx *search_test_ctx,
			    struct ldb_message *msg)
{
	char *full_dn1;
	char *full_dn2;
	const char *cn_vals[] = { "test_search_cn",
				  "test_search_cn2" };
	const char *uid_vals[] = { "test_search_uid",
				   "test_search_uid2" };
	const char *cn2_vals[] = { "test_search_2_cn",
				   "test_search_2_cn2" };
	const char *uid2_vals[] = { "test_search_2_uid",
				    "test_search_2_uid2" };

	full_dn1 = get_full_dn(search_test_ctx,
			       search_test_ctx,
			       "cn=test_search_cn");

	full_dn2 = get_full_dn(search_test_ctx,
			       search_test_ctx,
			       "cn=test_search_2_cn");

	if (has_dn(msg, full_dn1) == true) {
		assert_attr_has_vals(msg, "cn", cn_vals, 2);
		assert_attr_has_vals(msg, "uid", uid_vals, 2);
	} else if (has_dn(msg, full_dn2) == true) {
		assert_attr_has_vals(msg, "cn", cn2_vals, 2);
		assert_attr_has_vals(msg, "uid", uid2_vals, 2);
	} else {
		fail();
	}
}

static void test_search_match_both(void **state)
{
	struct search_test_ctx *search_test_ctx = talloc_get_type_abort(*state,
			struct search_test_ctx);
	int ret;
	struct ldb_dn *basedn;
	struct ldb_result *result = NULL;

	basedn = ldb_dn_new_fmt(search_test_ctx,
			        search_test_ctx->ldb_test_ctx->ldb,
				"%s",
				search_test_ctx->base_dn);
	assert_non_null(basedn);

	ret = ldb_search(search_test_ctx->ldb_test_ctx->ldb,
			 search_test_ctx,
			 &result,
			 basedn,
			 LDB_SCOPE_SUBTREE, NULL,
			 "cn=test_search_*");
	assert_int_equal(ret, 0);
	assert_non_null(result);
	assert_int_equal(result->count, 2);

	assert_expected(search_test_ctx, result->msgs[0]);
	assert_expected(search_test_ctx, result->msgs[1]);
}

static void test_search_match_basedn(void **state)
{
	struct search_test_ctx *search_test_ctx = talloc_get_type_abort(*state,
			struct search_test_ctx);
	int ret;
	struct ldb_dn *basedn;
	struct ldb_result *result = NULL;
	struct ldb_message *msg;

	basedn = ldb_dn_new_fmt(search_test_ctx,
			        search_test_ctx->ldb_test_ctx->ldb,
				"dc=nosuchdn");
	assert_non_null(basedn);

	ret = ldb_search(search_test_ctx->ldb_test_ctx->ldb,
			 search_test_ctx,
			 &result,
			 basedn,
			 LDB_SCOPE_SUBTREE, NULL,
			 "cn=*");
	assert_int_equal(ret, 0);

	/* Add 'checkBaseOnSearch' to @OPTIONS */
	msg = ldb_msg_new(search_test_ctx);
	assert_non_null(msg);

	msg->dn = ldb_dn_new_fmt(msg,
				 search_test_ctx->ldb_test_ctx->ldb,
				 "@OPTIONS");
	assert_non_null(msg->dn);

	ret = ldb_msg_add_string(msg, "checkBaseOnSearch", "TRUE");
	assert_int_equal(ret, 0);

	ret = ldb_add(search_test_ctx->ldb_test_ctx->ldb, msg);
	assert_int_equal(ret, 0);

	/* Search again */
	/* The search should return LDB_ERR_NO_SUCH_OBJECT */
	ret = ldb_search(search_test_ctx->ldb_test_ctx->ldb,
			 search_test_ctx,
			 &result,
			 basedn,
			 LDB_SCOPE_SUBTREE, NULL,
			 "cn=*");
	assert_int_equal(ret, LDB_ERR_NO_SUCH_OBJECT);

	ret = ldb_delete(search_test_ctx->ldb_test_ctx->ldb, msg->dn);
	assert_int_equal(ret, 0);
}


/*
 * This test is complex.
 * The purpose is to test for a deadlock detected between ldb_search()
 * and ldb_transaction_commit().  The deadlock happens if in process
 * (1) and (2):
 *  - (1) the all-record lock is taken in ltdb_search()
 *  - (2) the ldb_transaction_start() call is made
 *  - (1) an un-indexed search starts (forced here by doing it in
 *        the callback
 *  - (2) the ldb_transaction_commit() is called.
 *        This returns LDB_ERR_BUSY if the deadlock is detected
 *
 * With ldb 1.1.29 and tdb 1.3.12 we avoid this only due to a missing
 * lock call in ltdb_search() due to a refcounting bug in
 * ltdb_lock_read()
 */

struct search_against_transaction_ctx {
	struct ldbtest_ctx *test_ctx;
	int res_count;
	pid_t child_pid;
	struct ldb_dn *basedn;
};

static int test_ldb_search_against_transaction_callback2(struct ldb_request *req,
							 struct ldb_reply *ares)
{
	struct search_against_transaction_ctx *ctx = req->context;
	switch (ares->type) {
	case LDB_REPLY_ENTRY:
		ctx->res_count++;
		if (ctx->res_count != 1) {
			return LDB_SUCCESS;
		}

		break;

	case LDB_REPLY_REFERRAL:
		break;

	case LDB_REPLY_DONE:
		return ldb_request_done(req, LDB_SUCCESS);
	}

	return 0;

}

/*
 * This purpose of this callback is to trigger a transaction in
 * the child process while the all-record lock is held, but before
 * we take any locks in the tdb_traverse_read() handler.
 *
 * In tdb 1.3.12 tdb_traverse_read() take the read transaction lock
 * however in ldb 1.1.29 ltdb_search() forgets to take the all-record
 * lock (except the very first time) due to a ref-counting bug.
 *
 */

static int test_ldb_search_against_transaction_callback1(struct ldb_request *req,
							 struct ldb_reply *ares)
{
	int ret, ret2;
	int pipes[2];
	char buf[2];
	struct search_against_transaction_ctx *ctx = req->context;
	switch (ares->type) {
	case LDB_REPLY_ENTRY:
		break;

	case LDB_REPLY_REFERRAL:
		return LDB_SUCCESS;

	case LDB_REPLY_DONE:
		return ldb_request_done(req, LDB_SUCCESS);
	}

	ret = pipe(pipes);
	assert_int_equal(ret, 0);

	ctx->child_pid = fork();
	if (ctx->child_pid == 0) {
		TALLOC_CTX *tmp_ctx = NULL;
		struct ldb_message *msg;
		TALLOC_FREE(ctx->test_ctx->ldb);
		TALLOC_FREE(ctx->test_ctx->ev);
		ctx->test_ctx->ev = tevent_context_init(ctx->test_ctx);
		if (ctx->test_ctx->ev == NULL) {
			exit(LDB_ERR_OPERATIONS_ERROR);
		}

		ctx->test_ctx->ldb = ldb_init(ctx->test_ctx,
					      ctx->test_ctx->ev);
		if (ctx->test_ctx->ldb == NULL) {
			exit(LDB_ERR_OPERATIONS_ERROR);
		}

		ret = ldb_connect(ctx->test_ctx->ldb,
				  ctx->test_ctx->dbpath, 0, NULL);
		if (ret != LDB_SUCCESS) {
			exit(ret);
		}

		tmp_ctx = talloc_new(ctx->test_ctx);
		if (tmp_ctx == NULL) {
			exit(LDB_ERR_OPERATIONS_ERROR);
		}

		msg = ldb_msg_new(tmp_ctx);
		if (msg == NULL) {
			exit(LDB_ERR_OPERATIONS_ERROR);
		}

		msg->dn = ldb_dn_new_fmt(msg, ctx->test_ctx->ldb,
					 "dc=test");
		if (msg->dn == NULL) {
			exit(LDB_ERR_OPERATIONS_ERROR);
		}

		ret = ldb_msg_add_string(msg, "cn", "test_cn_val");
		if (ret != 0) {
			exit(LDB_ERR_OPERATIONS_ERROR);
		}

		ret = ldb_transaction_start(ctx->test_ctx->ldb);
		if (ret != 0) {
			exit(ret);
		}

		ret = write(pipes[1], "GO", 2);
		if (ret != 2) {
			exit(LDB_ERR_OPERATIONS_ERROR);
		}

		ret = ldb_add(ctx->test_ctx->ldb, msg);
		if (ret != 0) {
			exit(ret);
		}

		ret = ldb_transaction_commit(ctx->test_ctx->ldb);
		exit(ret);
	}

	ret = read(pipes[0], buf, 2);
	assert_int_equal(ret, 2);

	/* This search must be unindexed (ie traverse in tdb) */
	ret = ldb_build_search_req(&req,
				   ctx->test_ctx->ldb,
				   ctx->test_ctx,
				   ctx->basedn,
				   LDB_SCOPE_SUBTREE,
				   "cn=*", NULL,
				   NULL,
				   ctx,
				   test_ldb_search_against_transaction_callback2,
				   NULL);
	/*
	 * we don't assert on these return codes until after the search is
	 * finished, or the clean up will fail because we hold locks.
	 */

	ret2 = ldb_request(ctx->test_ctx->ldb, req);

	if (ret2 == LDB_SUCCESS) {
		ret2 = ldb_wait(req->handle, LDB_WAIT_ALL);
	}
	assert_int_equal(ret, 0);
	assert_int_equal(ret2, 0);
	assert_int_equal(ctx->res_count, 2);

	return LDB_SUCCESS;
}

static void test_ldb_search_against_transaction(void **state)
{
	struct search_test_ctx *search_test_ctx = talloc_get_type_abort(*state,
			struct search_test_ctx);
	struct search_against_transaction_ctx
		ctx =
		{ .res_count = 0,
		  .test_ctx = search_test_ctx->ldb_test_ctx
		};

	int ret;
	struct ldb_request *req;
	pid_t pid;
	int wstatus;
	struct ldb_dn *base_search_dn;

	tevent_loop_allow_nesting(search_test_ctx->ldb_test_ctx->ev);

	base_search_dn
		= ldb_dn_new_fmt(search_test_ctx,
				 search_test_ctx->ldb_test_ctx->ldb,
				 "cn=test_search_cn,%s",
				 search_test_ctx->base_dn);
	assert_non_null(base_search_dn);

	ctx.basedn
		= ldb_dn_new_fmt(search_test_ctx,
				 search_test_ctx->ldb_test_ctx->ldb,
				 "%s",
				 search_test_ctx->base_dn);
	assert_non_null(ctx.basedn);


	/* This search must be indexed (ie no traverse in tdb) */
	ret = ldb_build_search_req(&req,
				   search_test_ctx->ldb_test_ctx->ldb,
				   search_test_ctx,
				   base_search_dn,
				   LDB_SCOPE_BASE,
				   "cn=*", NULL,
				   NULL,
				   &ctx,
				   test_ldb_search_against_transaction_callback1,
				   NULL);
	assert_int_equal(ret, 0);
	ret = ldb_request(search_test_ctx->ldb_test_ctx->ldb, req);

	if (ret == LDB_SUCCESS) {
		ret = ldb_wait(req->handle, LDB_WAIT_ALL);
	}
	assert_int_equal(ret, 0);
	assert_int_equal(ctx.res_count, 2);

	pid = waitpid(ctx.child_pid, &wstatus, 0);
	assert_int_equal(pid, ctx.child_pid);

	assert_true(WIFEXITED(wstatus));

	assert_int_equal(WEXITSTATUS(wstatus), 0);


}

static int ldb_case_test_setup(void **state)
{
	int ret;
	struct ldb_ldif *ldif;
	struct ldbtest_ctx *ldb_test_ctx;
	const char *attrs_ldif =  \
		"dn: @ATTRIBUTES\n"
		"cn: CASE_INSENSITIVE\n"
		"\n";
	struct keyval kvs[] = {
		{ "cn", "CaseInsensitiveValue" },
		{ "uid", "CaseSensitiveValue" },
		{ NULL, NULL },
	};


	ldbtest_setup((void **) &ldb_test_ctx);

	while ((ldif = ldb_ldif_read_string(ldb_test_ctx->ldb, &attrs_ldif))) {
		ret = ldb_add(ldb_test_ctx->ldb, ldif->msg);
		assert_int_equal(ret, LDB_SUCCESS);
	}

	ldb_test_add_data(ldb_test_ctx,
			  ldb_test_ctx,
			  "cn=CaseInsensitiveValue",
			  kvs);

	*state = ldb_test_ctx;
	return 0;
}

static int ldb_case_test_teardown(void **state)
{
	int ret;
	struct ldbtest_ctx *ldb_test_ctx = talloc_get_type_abort(*state,
			struct ldbtest_ctx);

	struct ldb_dn *del_dn;

	del_dn = ldb_dn_new_fmt(ldb_test_ctx,
				ldb_test_ctx->ldb,
				"@ATTRIBUTES");
	assert_non_null(del_dn);

	ret = ldb_delete(ldb_test_ctx->ldb, del_dn);
	assert_int_equal(ret, LDB_SUCCESS);

	assert_dn_doesnt_exist(ldb_test_ctx,
			       "@ATTRIBUTES");

	ldb_test_remove_data(ldb_test_ctx, ldb_test_ctx,
			     "cn=CaseInsensitiveValue");

	ldbtest_teardown((void **) &ldb_test_ctx);
	return 0;
}

static void test_ldb_attrs_case_insensitive(void **state)
{
	int cnt;
	struct ldbtest_ctx *ldb_test_ctx = talloc_get_type_abort(*state,
			struct ldbtest_ctx);

	/* cn matches exact case */
	cnt = sub_search_count(ldb_test_ctx, "", "cn=CaseInsensitiveValue");
	assert_int_equal(cnt, 1);

	/* cn matches lower case */
	cnt = sub_search_count(ldb_test_ctx, "", "cn=caseinsensitivevalue");
	assert_int_equal(cnt, 1);

	/* uid matches exact case */
	cnt = sub_search_count(ldb_test_ctx, "", "uid=CaseSensitiveValue");
	assert_int_equal(cnt, 1);

	/* uid does not match lower case */
	cnt = sub_search_count(ldb_test_ctx, "", "uid=casesensitivevalue");
	assert_int_equal(cnt, 0);
}

static struct ldb_schema_attribute cn_attr_1;
static struct ldb_schema_attribute cn_attr_2;
static struct ldb_schema_attribute default_attr;

/*
  override the name to attribute handler function
 */
static const struct ldb_schema_attribute *ldb_test_attribute_handler_override(struct ldb_context *ldb,
									      void *private_data,
									      const char *name)
{
	if (private_data != NULL && ldb_attr_cmp(name, "cn") == 0) {
		return &cn_attr_1;
	} else if (private_data == NULL && ldb_attr_cmp(name, "cn") == 0) {
		return &cn_attr_2;
	} else if (ldb_attr_cmp(name, "uid") == 0) {
		return &cn_attr_2;
	}
	return &default_attr;
}

static void test_ldb_attrs_case_handler(void **state)
{
	int cnt;
	int ret;
	const struct ldb_schema_syntax *syntax;

	struct ldbtest_ctx *ldb_test_ctx = talloc_get_type_abort(*state,
			struct ldbtest_ctx);
	struct ldb_context *ldb = ldb_test_ctx->ldb;

	/* cn matches lower case */
	cnt = sub_search_count(ldb_test_ctx, "", "cn=caseinsensitivevalue");
	assert_int_equal(cnt, 1);

	syntax = ldb_standard_syntax_by_name(ldb, LDB_SYNTAX_OCTET_STRING);
	assert_non_null(syntax);

	ret = ldb_schema_attribute_fill_with_syntax(ldb, ldb,
						    "*", 0,
						    syntax, &default_attr);
	assert_int_equal(ret, LDB_SUCCESS);

	syntax = ldb_standard_syntax_by_name(ldb, LDB_SYNTAX_OCTET_STRING);
	assert_non_null(syntax);

	ret = ldb_schema_attribute_fill_with_syntax(ldb, ldb,
						    "cn", 0,
						    syntax, &cn_attr_1);
	assert_int_equal(ret, LDB_SUCCESS);

	/*
	 * Set an attribute handler, which will fail to match as we
	 * force case sensitive
	 */
	ldb_schema_attribute_set_override_handler(ldb,
						  ldb_test_attribute_handler_override,
						  (void *)1);

	/* cn does not matche lower case */
	cnt = sub_search_count(ldb_test_ctx, "", "cn=caseinsensitivevalue");
	assert_int_equal(cnt, 0);

	syntax = ldb_standard_syntax_by_name(ldb, LDB_SYNTAX_DIRECTORY_STRING);
	assert_non_null(syntax);

	ret = ldb_schema_attribute_fill_with_syntax(ldb, ldb,
						    "cn", 0,
						    syntax, &cn_attr_2);
	assert_int_equal(ret, LDB_SUCCESS);

	/*
	 * Set an attribute handler, which will match as we
	 * force case insensitive
	 */
	ldb_schema_attribute_set_override_handler(ldb,
						  ldb_test_attribute_handler_override,
						  NULL);

	/* cn matches lower case */
	cnt = sub_search_count(ldb_test_ctx, "", "cn=caseinsensitivevalue");
	assert_int_equal(cnt, 1);

}


static void test_ldb_attrs_index_handler(void **state)
{
	int cnt;
	int ret;
	const struct ldb_schema_syntax *syntax;
	struct ldb_ldif *ldif;

	const char *index_ldif =  \
		"dn: @INDEXLIST\n"
		"@IDXATTR: cn\n"
		"\n";

	struct ldbtest_ctx *ldb_test_ctx = talloc_get_type_abort(*state,
			struct ldbtest_ctx);
	struct ldb_context *ldb = ldb_test_ctx->ldb;

	/* cn matches lower case */
	cnt = sub_search_count(ldb_test_ctx, "", "cn=caseinsensitivevalue");
	assert_int_equal(cnt, 1);

	syntax = ldb_standard_syntax_by_name(ldb, LDB_SYNTAX_OCTET_STRING);
	assert_non_null(syntax);

	ret = ldb_schema_attribute_fill_with_syntax(ldb, ldb,
						    "cn", 0,
						    syntax, &cn_attr_1);
	assert_int_equal(ret, LDB_SUCCESS);

	syntax = ldb_standard_syntax_by_name(ldb, LDB_SYNTAX_DIRECTORY_STRING);
	assert_non_null(syntax);

	ret = ldb_schema_attribute_fill_with_syntax(ldb, ldb,
						    "cn", LDB_ATTR_FLAG_INDEXED,
						    syntax, &cn_attr_2);
	assert_int_equal(ret, LDB_SUCCESS);

	/*
	 * Set an attribute handler
	 */
	ldb_schema_attribute_set_override_handler(ldb,
						  ldb_test_attribute_handler_override,
						  NULL);

	/* cn matches lower case */
	cnt = sub_search_count(ldb_test_ctx, "", "cn=caseinsensitivevalue");
	assert_int_equal(cnt, 1);

	/* Add the index (actually any modify will do) */
	while ((ldif = ldb_ldif_read_string(ldb_test_ctx->ldb, &index_ldif))) {
		ret = ldb_add(ldb_test_ctx->ldb, ldif->msg);
		assert_int_equal(ret, LDB_SUCCESS);
	}

	ldb_schema_set_override_indexlist(ldb, false);

	/* cn does match as there is an index now */
	cnt = sub_search_count(ldb_test_ctx, "", "cn=caseinsensitivevalue");
	assert_int_equal(cnt, 1);

	/*
	 * Set an attribute handler, which will later fail to match as we
	 * didn't re-index the DB
	 */
	ldb_schema_attribute_set_override_handler(ldb,
						  ldb_test_attribute_handler_override,
						  (void *)1);

	/*
	 * cn does not match as we changed the case sensitivity, but
	 * didn't re-index
	 *
	 * This shows that the override is in control
	 */
	cnt = sub_search_count(ldb_test_ctx, "", "cn=caseinsensitivevalue");
	assert_int_equal(cnt, 0);

}

static int ldb_case_attrs_index_test_teardown(void **state)
{
	int ret;
	struct ldbtest_ctx *ldb_test_ctx = talloc_get_type_abort(*state,
			struct ldbtest_ctx);
	struct ldb_dn *del_dn;

	del_dn = ldb_dn_new_fmt(ldb_test_ctx,
				ldb_test_ctx->ldb,
				"@INDEXLIST");
	assert_non_null(del_dn);

	ret = ldb_delete(ldb_test_ctx->ldb, del_dn);
	if (ret != LDB_ERR_NO_SUCH_OBJECT) {
		assert_int_equal(ret, LDB_SUCCESS);
	}

	assert_dn_doesnt_exist(ldb_test_ctx,
			       "@INDEXLIST");

	ldb_case_test_teardown(state);
	return 0;
}


struct rename_test_ctx {
	struct ldbtest_ctx *ldb_test_ctx;

	struct ldb_dn *basedn;
	const char *str_basedn;

	const char *teardown_dn;
};

static int ldb_rename_test_setup(void **state)
{
	struct ldbtest_ctx *ldb_test_ctx;
	struct rename_test_ctx *rename_test_ctx;
	const char *strdn = "dc=rename_test_entry_from";

	ldbtest_setup((void **) &ldb_test_ctx);

	rename_test_ctx = talloc(ldb_test_ctx, struct rename_test_ctx);
	assert_non_null(rename_test_ctx);
	rename_test_ctx->ldb_test_ctx = ldb_test_ctx;
	assert_non_null(rename_test_ctx->ldb_test_ctx);

	rename_test_ctx->basedn = ldb_dn_new_fmt(rename_test_ctx,
				rename_test_ctx->ldb_test_ctx->ldb,
				"%s", strdn);
	assert_non_null(rename_test_ctx->basedn);

	rename_test_ctx->str_basedn = strdn;
	rename_test_ctx->teardown_dn = strdn;

	add_dn_with_cn(ldb_test_ctx,
		       rename_test_ctx->basedn,
		       "test_rename_cn_val");

	*state = rename_test_ctx;
	return 0;
}

static int ldb_rename_test_teardown(void **state)
{
	int ret;
	struct rename_test_ctx *rename_test_ctx = talloc_get_type_abort(*state,
			struct rename_test_ctx);
	struct ldbtest_ctx *ldb_test_ctx;
	struct ldb_dn *del_dn;

	ldb_test_ctx = rename_test_ctx->ldb_test_ctx;

	del_dn = ldb_dn_new_fmt(rename_test_ctx,
				rename_test_ctx->ldb_test_ctx->ldb,
				"%s", rename_test_ctx->teardown_dn);
	assert_non_null(del_dn);

	ret = ldb_delete(ldb_test_ctx->ldb, del_dn);
	assert_int_equal(ret, LDB_SUCCESS);

	assert_dn_doesnt_exist(ldb_test_ctx,
			       rename_test_ctx->teardown_dn);

	ldbtest_teardown((void **) &ldb_test_ctx);
	return 0;
}

static void test_ldb_rename(void **state)
{
	struct rename_test_ctx *rename_test_ctx =
		talloc_get_type_abort(*state, struct rename_test_ctx);
	int ret;
	const char *str_new_dn = "dc=rename_test_entry_to";
	struct ldb_dn *new_dn;

	new_dn = ldb_dn_new_fmt(rename_test_ctx,
				rename_test_ctx->ldb_test_ctx->ldb,
				"%s", str_new_dn);
	assert_non_null(new_dn);

	ret = ldb_rename(rename_test_ctx->ldb_test_ctx->ldb,
			 rename_test_ctx->basedn,
			 new_dn);
	assert_int_equal(ret, LDB_SUCCESS);

	assert_dn_exists(rename_test_ctx->ldb_test_ctx, str_new_dn);
	assert_dn_doesnt_exist(rename_test_ctx->ldb_test_ctx,
			       rename_test_ctx->str_basedn);
	rename_test_ctx->teardown_dn = str_new_dn;

	/* FIXME - test the values which didn't change */
}

static void test_ldb_rename_from_doesnt_exist(void **state)
{
	struct rename_test_ctx *rename_test_ctx = talloc_get_type_abort(
							*state,
							struct rename_test_ctx);
	int ret;
	const char *str_new_dn = "dc=rename_test_entry_to";
	const char *str_bad_old_dn = "dc=rename_test_no_such_entry";
	struct ldb_dn *new_dn;
	struct ldb_dn *bad_old_dn;

	new_dn = ldb_dn_new_fmt(rename_test_ctx,
				rename_test_ctx->ldb_test_ctx->ldb,
				"%s", str_new_dn);
	assert_non_null(new_dn);

	bad_old_dn = ldb_dn_new_fmt(rename_test_ctx,
				    rename_test_ctx->ldb_test_ctx->ldb,
				    "%s", str_bad_old_dn);
	assert_non_null(bad_old_dn);

	assert_dn_doesnt_exist(rename_test_ctx->ldb_test_ctx,
			       str_bad_old_dn);

	ret = ldb_rename(rename_test_ctx->ldb_test_ctx->ldb,
			 bad_old_dn, new_dn);
	assert_int_equal(ret, LDB_ERR_NO_SUCH_OBJECT);

	assert_dn_doesnt_exist(rename_test_ctx->ldb_test_ctx,
			       str_new_dn);
}

static void test_ldb_rename_to_exists(void **state)
{
	struct rename_test_ctx *rename_test_ctx = talloc_get_type_abort(
							*state,
							struct rename_test_ctx);
	int ret;
	const char *str_new_dn = "dc=rename_test_already_exists";
	struct ldb_dn *new_dn;

	new_dn = ldb_dn_new_fmt(rename_test_ctx,
				rename_test_ctx->ldb_test_ctx->ldb,
				"%s", str_new_dn);
	assert_non_null(new_dn);

	add_dn_with_cn(rename_test_ctx->ldb_test_ctx,
		       new_dn,
		       "test_rename_cn_val");

	ret = ldb_rename(rename_test_ctx->ldb_test_ctx->ldb,
			 rename_test_ctx->basedn,
			 new_dn);
	assert_int_equal(ret, LDB_ERR_ENTRY_ALREADY_EXISTS);

	/* Old object must still exist */
	assert_dn_exists(rename_test_ctx->ldb_test_ctx,
			 rename_test_ctx->str_basedn);

	ret = ldb_delete(rename_test_ctx->ldb_test_ctx->ldb,
			 new_dn);
	assert_int_equal(ret, LDB_SUCCESS);

	assert_dn_exists(rename_test_ctx->ldb_test_ctx,
			       rename_test_ctx->teardown_dn);
}

static void test_ldb_rename_self(void **state)
{
	struct rename_test_ctx *rename_test_ctx = talloc_get_type_abort(
							*state,
							struct rename_test_ctx);
	int ret;

	/* Oddly enough, this is a success in ldb.. */
	ret = ldb_rename(rename_test_ctx->ldb_test_ctx->ldb,
			 rename_test_ctx->basedn,
			 rename_test_ctx->basedn);
	assert_int_equal(ret, LDB_SUCCESS);

	/* Old object must still exist */
	assert_dn_exists(rename_test_ctx->ldb_test_ctx,
			 rename_test_ctx->str_basedn);
}

static void test_ldb_rename_dn_case_change(void **state)
{
	struct rename_test_ctx *rename_test_ctx = talloc_get_type_abort(
							*state,
							struct rename_test_ctx);
	int ret;
	char *str_new_dn;
	struct ldb_dn *new_dn;
	unsigned i;

	str_new_dn = talloc_strdup(rename_test_ctx, rename_test_ctx->str_basedn);
	assert_non_null(str_new_dn);
	for (i = 0; str_new_dn[i]; i++) {
		str_new_dn[i] = toupper(str_new_dn[i]);
	}

	new_dn = ldb_dn_new_fmt(rename_test_ctx,
				rename_test_ctx->ldb_test_ctx->ldb,
				"%s", str_new_dn);
	assert_non_null(new_dn);

	ret = ldb_rename(rename_test_ctx->ldb_test_ctx->ldb,
			 rename_test_ctx->basedn,
			 new_dn);
	assert_int_equal(ret, LDB_SUCCESS);

	/* DNs are case insensitive, so both searches will match */
	assert_dn_exists(rename_test_ctx->ldb_test_ctx, str_new_dn);
	assert_dn_exists(rename_test_ctx->ldb_test_ctx,
			 rename_test_ctx->str_basedn);
	/* FIXME - test the values didn't change */
}

int main(int argc, const char **argv)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup_teardown(test_connect,
						ldbtest_noconn_setup,
						ldbtest_noconn_teardown),
		cmocka_unit_test_setup_teardown(test_ldb_add,
						ldbtest_setup,
						ldbtest_teardown),
		cmocka_unit_test_setup_teardown(test_ldb_search,
						ldbtest_setup,
						ldbtest_teardown),
		cmocka_unit_test_setup_teardown(test_ldb_del,
						ldbtest_setup,
						ldbtest_teardown),
		cmocka_unit_test_setup_teardown(test_ldb_del_noexist,
						ldbtest_setup,
						ldbtest_teardown),
		cmocka_unit_test_setup_teardown(test_ldb_handle,
						ldbtest_setup,
						ldbtest_teardown),
		cmocka_unit_test_setup_teardown(test_ldb_build_search_req,
						ldbtest_setup,
						ldbtest_teardown),
		cmocka_unit_test_setup_teardown(test_transactions,
						ldbtest_setup,
						ldbtest_teardown),
		cmocka_unit_test_setup_teardown(test_ldb_modify_add_key,
						ldb_modify_test_setup,
						ldb_modify_test_teardown),
		cmocka_unit_test_setup_teardown(test_ldb_modify_extend_key,
						ldb_modify_test_setup,
						ldb_modify_test_teardown),
		cmocka_unit_test_setup_teardown(test_ldb_modify_add_key_noval,
						ldb_modify_test_setup,
						ldb_modify_test_teardown),
		cmocka_unit_test_setup_teardown(test_ldb_modify_replace_key,
						ldb_modify_test_setup,
						ldb_modify_test_teardown),
		cmocka_unit_test_setup_teardown(test_ldb_modify_replace_noexist_key,
						ldb_modify_test_setup,
						ldb_modify_test_teardown),
		cmocka_unit_test_setup_teardown(test_ldb_modify_replace_zero_vals,
						ldb_modify_test_setup,
						ldb_modify_test_teardown),
		cmocka_unit_test_setup_teardown(test_ldb_modify_replace_noexist_key_zero_vals,
						ldb_modify_test_setup,
						ldb_modify_test_teardown),
		cmocka_unit_test_setup_teardown(test_ldb_modify_del_key,
						ldb_modify_test_setup,
						ldb_modify_test_teardown),
		cmocka_unit_test_setup_teardown(test_ldb_modify_del_keyval,
						ldb_modify_test_setup,
						ldb_modify_test_teardown),
		cmocka_unit_test_setup_teardown(test_search_match_none,
						ldb_search_test_setup,
						ldb_search_test_teardown),
		cmocka_unit_test_setup_teardown(test_search_match_one,
						ldb_search_test_setup,
						ldb_search_test_teardown),
		cmocka_unit_test_setup_teardown(test_search_match_filter,
						ldb_search_test_setup,
						ldb_search_test_teardown),
		cmocka_unit_test_setup_teardown(test_search_match_both,
						ldb_search_test_setup,
						ldb_search_test_teardown),
		cmocka_unit_test_setup_teardown(test_search_match_basedn,
						ldb_search_test_setup,
						ldb_search_test_teardown),
		cmocka_unit_test_setup_teardown(test_ldb_search_against_transaction,
						ldb_search_test_setup,
						ldb_search_test_teardown),
		cmocka_unit_test_setup_teardown(test_ldb_attrs_case_insensitive,
						ldb_case_test_setup,
						ldb_case_test_teardown),
		cmocka_unit_test_setup_teardown(test_ldb_attrs_case_handler,
						ldb_case_test_setup,
						ldb_case_test_teardown),
		cmocka_unit_test_setup_teardown(test_ldb_attrs_index_handler,
						ldb_case_test_setup,
						ldb_case_attrs_index_test_teardown),
		cmocka_unit_test_setup_teardown(test_ldb_rename,
						ldb_rename_test_setup,
						ldb_rename_test_teardown),
		cmocka_unit_test_setup_teardown(test_ldb_rename_from_doesnt_exist,
						ldb_rename_test_setup,
						ldb_rename_test_teardown),
		cmocka_unit_test_setup_teardown(test_ldb_rename_to_exists,
						ldb_rename_test_setup,
						ldb_rename_test_teardown),
		cmocka_unit_test_setup_teardown(test_ldb_rename_self,
						ldb_rename_test_setup,
						ldb_rename_test_teardown),
		cmocka_unit_test_setup_teardown(test_ldb_rename_dn_case_change,
						ldb_rename_test_setup,
						ldb_rename_test_teardown),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
