#include "replace.h"
#include "system/filesys.h"
#include "system/time.h"
#include "tdb.h"
#include "ldb_module.h"

/* this private structure is used by the ltdb backend in the
   ldb_context */
struct ltdb_private {
	TDB_CONTEXT *tdb;
	unsigned int connect_flags;
	
	unsigned long long sequence_number;

	/* the low level tdb seqnum - used to avoid loading BASEINFO when
	   possible */
	int tdb_seqnum;

	struct ltdb_cache {
		struct ldb_message *indexlist;
		bool one_level_indexes;
		bool attribute_indexes;
		const char *GUID_index_attribute;
		const char *GUID_index_dn_component;
	} *cache;

	int in_transaction;

	bool check_base;
	bool disallow_dn_filter;
	struct ltdb_idxptr *idxptr;
	bool prepared_commit;
	int read_lock_count;

	bool warn_unindexed;
	bool warn_reindex;

	bool read_only;

	const struct ldb_schema_syntax *GUID_index_syntax;
};

struct ltdb_context {
	struct ldb_module *module;
	struct ldb_request *req;

	bool request_terminated;
	struct ltdb_req_spy *spy;

	/* search stuff */
	const struct ldb_parse_tree *tree;
	struct ldb_dn *base;
	enum ldb_scope scope;
	const char * const *attrs;
	struct tevent_timer *timeout_event;

	/* error handling */
	int error;
};

/* special record types */
#define LTDB_INDEX      "@INDEX"
#define LTDB_INDEXLIST  "@INDEXLIST"
#define LTDB_IDX        "@IDX"
#define LTDB_IDXVERSION "@IDXVERSION"
#define LTDB_IDXATTR    "@IDXATTR"
#define LTDB_IDXONE     "@IDXONE"
#define LTDB_IDXDN     "@IDXDN"
#define LTDB_IDXGUID    "@IDXGUID"
#define LTDB_IDX_DN_GUID "@IDX_DN_GUID"
#define LTDB_BASEINFO   "@BASEINFO"
#define LTDB_OPTIONS    "@OPTIONS"
#define LTDB_ATTRIBUTES "@ATTRIBUTES"

/* special attribute types */
#define LTDB_SEQUENCE_NUMBER "sequenceNumber"
#define LTDB_CHECK_BASE "checkBaseOnSearch"
#define LTDB_DISALLOW_DN_FILTER "disallowDNFilter"
#define LTDB_MOD_TIMESTAMP "whenChanged"
#define LTDB_OBJECTCLASS "objectClass"

/* DB keys */
#define LTDB_GUID_KEY_PREFIX "GUID="
#define LTDB_GUID_SIZE 16

/* The following definitions come from lib/ldb/ldb_tdb/ldb_cache.c  */

int ltdb_cache_reload(struct ldb_module *module);
int ltdb_cache_load(struct ldb_module *module);
int ltdb_increase_sequence_number(struct ldb_module *module);
int ltdb_check_at_attributes_values(const struct ldb_val *value);

/* The following definitions come from lib/ldb/ldb_tdb/ldb_index.c  */

struct ldb_parse_tree;

int ltdb_search_indexed(struct ltdb_context *ctx, uint32_t *);
int ltdb_index_add_new(struct ldb_module *module,
		       struct ltdb_private *ltdb,
		       const struct ldb_message *msg);
int ltdb_index_delete(struct ldb_module *module, const struct ldb_message *msg);
int ltdb_index_del_element(struct ldb_module *module,
			   struct ltdb_private *ltdb,
			   const struct ldb_message *msg,
			   struct ldb_message_element *el);
int ltdb_index_add_element(struct ldb_module *module,
			   struct ltdb_private *ltdb,
			   const struct ldb_message *msg,
			   struct ldb_message_element *el);
int ltdb_index_del_value(struct ldb_module *module,
			 struct ltdb_private *ltdb,
			 const struct ldb_message *msg,
			 struct ldb_message_element *el, unsigned int v_idx);
int ltdb_reindex(struct ldb_module *module);
int ltdb_index_transaction_start(struct ldb_module *module);
int ltdb_index_transaction_commit(struct ldb_module *module);
int ltdb_index_transaction_cancel(struct ldb_module *module);
int ltdb_key_dn_from_idx(struct ldb_module *module,
			 struct ltdb_private *ltdb,
			 TALLOC_CTX *mem_ctx,
			 struct ldb_dn *dn,
			 TDB_DATA *tdb_key);

/* The following definitions come from lib/ldb/ldb_tdb/ldb_search.c  */

int ltdb_has_wildcard(struct ldb_module *module, const char *attr_name, 
		      const struct ldb_val *val);
void ltdb_search_dn1_free(struct ldb_module *module, struct ldb_message *msg);
int ltdb_search_dn1(struct ldb_module *module, struct ldb_dn *dn, struct ldb_message *msg,
		    unsigned int unpack_flags);
int ltdb_search_base(struct ldb_module *module, struct ldb_dn *dn);
int ltdb_search_key(struct ldb_module *module, struct ltdb_private *ltdb,
		    struct TDB_DATA tdb_key,
		    struct ldb_message *msg,
		    unsigned int unpack_flags);
int ltdb_filter_attrs(TALLOC_CTX *mem_ctx,
		      const struct ldb_message *msg, const char * const *attrs,
		      struct ldb_message **filtered_msg);
int ltdb_search(struct ltdb_context *ctx);

/* The following definitions come from lib/ldb/ldb_tdb/ldb_tdb.c  */
int ltdb_lock_read(struct ldb_module *module);
int ltdb_unlock_read(struct ldb_module *module);
/* 
 * Determine if this key could hold a record.  We allow the new GUID
 * index, the old DN index and a possible future ID=
 */
bool ltdb_key_is_record(TDB_DATA key);
TDB_DATA ltdb_key_dn(struct ldb_module *module, struct ldb_dn *dn);
TDB_DATA ltdb_key_msg(struct ldb_module *module, const struct ldb_message *msg);
TDB_DATA ltdb_guid_to_key(struct ldb_module *module,
			  struct ltdb_private *ltdb,
			  TALLOC_CTX *mem_ctx,
			  const struct ldb_val *guid_val);
int ltdb_store(struct ldb_module *module, const struct ldb_message *msg, int flgs);
int ltdb_modify_internal(struct ldb_module *module, const struct ldb_message *msg, struct ldb_request *req);
int ltdb_delete_noindex(struct ldb_module *module,
			const struct ldb_message *msg);
int ltdb_err_map(enum TDB_ERROR tdb_code);

struct tdb_context *ltdb_wrap_open(TALLOC_CTX *mem_ctx,
				   const char *path, int hash_size, int tdb_flags,
				   int open_flags, mode_t mode,
				   struct ldb_context *ldb);
