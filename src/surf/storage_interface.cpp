#include "storage_interface.hpp"
#include "surf_private.h"

XBT_LOG_NEW_DEFAULT_SUBCATEGORY(surf_storage, surf,
                                "Logging specific to the SURF storage module");

xbt_lib_t storage_lib;
int ROUTING_STORAGE_LEVEL;      //Routing for storagelevel
int ROUTING_STORAGE_HOST_LEVEL;
int SURF_STORAGE_LEVEL;
xbt_lib_t storage_type_lib;
int ROUTING_STORAGE_TYPE_LEVEL; //Routing for storage_type level

xbt_dynar_t mount_list = NULL;
StorageModelPtr surf_storage_model = NULL;

/*********
 * Model *
 *********/

StorageModel::StorageModel() : Model("Storage") {
  p_storageList = NULL;
}

StorageModel::~StorageModel(){
  lmm_system_free(p_maxminSystem);

  surf_storage_model = NULL;

  xbt_dynar_free(&p_storageList);
}

/************
 * Resource *
 ************/

Storage::Storage(ModelPtr model, const char *name, xbt_dict_t props,
		         const char* type_id, char *content_name, char *content_type, sg_size_t size)
 : Resource(model, name, props)
 , p_contentType(content_type)
 , m_size(size), m_usedSize(0)
 , p_typeId(xbt_strdup(type_id))
 , p_writeActions(xbt_dynar_new(sizeof(ActionPtr),NULL))
{
  p_content = parseContent(content_name);
  m_stateCurrent = SURF_RESOURCE_ON;
}

Storage::Storage(ModelPtr model, const char *name, xbt_dict_t props,
		         lmm_system_t maxminSystem, double bread, double bwrite, double bconnection,
	             const char* type_id, char *content_name, char *content_type, sg_size_t size)
 :  Resource(model, name, props, lmm_constraint_new(maxminSystem, this, bconnection))
 , p_contentType(content_type)
 , m_size(size), m_usedSize(0)
 , p_typeId(xbt_strdup(type_id))
 , p_writeActions(xbt_dynar_new(sizeof(ActionPtr),NULL)) {
  p_content = parseContent(content_name);
  m_stateCurrent = SURF_RESOURCE_ON;
  XBT_DEBUG("Create resource with Bconnection '%f' Bread '%f' Bwrite '%f' and Size '%llu'", bconnection, bread, bwrite, size);
  p_constraintRead  = lmm_constraint_new(maxminSystem, this, bread);
  p_constraintWrite = lmm_constraint_new(maxminSystem, this, bwrite);
}

Storage::~Storage(){
  xbt_dict_free(&p_content);
  xbt_dynar_free(&p_writeActions);
  free(p_typeId);
  free(p_contentType);
}

xbt_dict_t Storage::parseContent(char *filename)
{
  m_usedSize = 0;
  if ((!filename) || (strcmp(filename, "") == 0))
    return NULL;

  xbt_dict_t parse_content = xbt_dict_new_homogeneous(xbt_free);
  FILE *file = NULL;

  file = surf_fopen(filename, "r");
  xbt_assert(file != NULL, "Cannot open file '%s' (path=%s)", filename,
              xbt_str_join(surf_path, ":"));

  char *line = NULL;
  size_t len = 0;
  ssize_t read;
  char path[1024];
  sg_size_t size;


  while ((read = xbt_getline(&line, &len, file)) != -1) {
    if (read){
    if(sscanf(line,"%s %llu", path, &size) == 2) {
        m_usedSize += size;
        sg_size_t *psize = xbt_new(sg_size_t, 1);
        *psize = size;
        xbt_dict_set(parse_content,path,psize,NULL);
      } else {
        xbt_die("Be sure of passing a good format for content file.\n");
      }
    }
  }
  free(line);
  fclose(file);
  return parse_content;
}

bool Storage::isUsed()
{
  THROW_UNIMPLEMENTED;
  return false;
}

void Storage::updateState(tmgr_trace_event_t /*event_type*/, double /*value*/, double /*date*/)
{
  THROW_UNIMPLEMENTED;
}

xbt_dict_t Storage::getContent()
{
  /* For the moment this action has no cost, but in the future we could take in account access latency of the disk */
  /*surf_action_t action = storage_action_execute(storage,0, LS);*/

  xbt_dict_t content_dict = xbt_dict_new_homogeneous(NULL);
  xbt_dict_cursor_t cursor = NULL;
  char *file;
  sg_size_t *psize;

  xbt_dict_foreach(p_content, cursor, file, psize){
    xbt_dict_set(content_dict,file,psize,NULL);
  }
  return content_dict;
}

sg_size_t Storage::getSize(){
  return m_size;
}



/**********
 * Action *
 **********/
StorageAction::StorageAction(ModelPtr model, double cost, bool failed,
		                     StoragePtr storage, e_surf_action_storage_type_t type)
: Action(model, cost, failed)
, m_type(type), p_storage(storage), p_file(NULL), p_lsDict(NULL)
{
};

StorageAction::StorageAction(ModelPtr model, double cost, bool failed, lmm_variable_t var,
		                     StoragePtr storage, e_surf_action_storage_type_t type)
  : Action(model, cost, failed, var)
  , m_type(type), p_storage(storage), p_file(NULL), p_lsDict(NULL) {
}
