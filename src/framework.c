
/*
 * (C) 2010-2011 Alibaba Group Holding Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */


#include "tsar.h"


void
register_mod_fileds(struct module *mod, const char *opt, const char *usage,
    struct mod_info *info, int n_col, void *data_collect, void *set_st_record)
{
    sprintf(mod->opt_line, "%s", opt);
    sprintf(mod->usage, "%s", usage);
    mod->info = info;
    mod->n_col = n_col;
    mod->data_collect = data_collect;
    mod->set_st_record = set_st_record;
}


void
set_mod_record(struct module *mod, const char *record)
{
    if (record) {
        sprintf(mod->record, "%s", record);
    }
}


/*
 * load module from dir
 */
void
load_modules()
{//根据模块名，dlopen加载模块，然后调用每个模块的mod_register函数
    int     i;
    char    buff[LEN_128] = {0};
    char    mod_path[LEN_128] = {0};
    struct  module *mod = NULL;
    int    (*mod_register) (struct module *);

    /* get the full path of modules */
    sprintf(buff, "/usr/local/tsar/modules");

    for (i = 0; i < statis.total_mod_num; i++) {
        mod = &mods[i];
        if (!mod->lib) {
            memset(mod_path, '\0', LEN_128);
            snprintf(mod_path, LEN_128, "%s/%s.so", buff, mod->name);
            if (!(mod->lib = dlopen(mod_path, RTLD_NOW|RTLD_GLOBAL))) {
                do_debug(LOG_ERR, "load_modules: dlopen module %s err %s\n", mod->name, dlerror());

            } else {
                mod_register = dlsym(mod->lib, "mod_register");
                if (dlerror()) {
                    do_debug(LOG_ERR, "load_modules: dlsym module %s err %s\n", mod->name, dlerror());
                    break;

                } else {
                    mod_register(mod);//调用其mod_register函数。这些函数一般会调用register_mod_fileds注册get/set回调的。
                    mod->enable = 1;
                    mod->spec = 0;
                    do_debug(LOG_INFO, "load_modules: load new module '%s' to mods\n", mod_path);
                }
            }
        }
    }
}


/*
 * module name must be composed by alpha/number/_
 * match return 1
 */
int
is_include_string(const char *mods, const char *mod)
{
    char   *token, n_str[LEN_512] = {0};

    memcpy(n_str, mods, strlen(mods));

    token = strtok(n_str, DATA_SPLIT);
    while (token) {
        if (!strcmp(token, mod)) {
            return 1;
        }
        token = strtok(NULL, DATA_SPLIT);
    }
    return 0;
}


/*
 * reload modules by mods, if not find in mods, then set module disable
 * return 1 if mod load ok
 * return 0 else
 */
int
reload_modules(const char *s_mod)
{
    int    i;
    int    reload = 0;
    struct module *mod;

    if (!s_mod || !strlen(s_mod)) {
        return reload;
    }
//根据模块查找，找到后将enable=1，否则=0，然后返回是否找到。但是没干其他事情。什么意思
    for (i = 0; i < statis.total_mod_num; i++) {
        mod = &mods[i];
        if (is_include_string(s_mod, mod->name) || is_include_string(s_mod, mod->opt_line)) {
            mod->enable = 1;
            reload = 1;

        } else {
            mod->enable = 0;
        }
    }
    return reload;
}

#ifdef OLDTSAR
/*
 * reload check modules by mods, if not find in mods, then set module disable
 */
void
reload_check_modules()
{
    int    i;
    struct module *mod;

    for (i = 0; i < statis.total_mod_num; i++) {
        mod = &mods[i];
        if (!strcmp(mod->name, "mod_apache")
             || !strcmp(mod->name, "mod_cpu")
             || !strcmp(mod->name, "mod_mem")
             || !strcmp(mod->name, "mod_load")
             || !strcmp(mod->name, "mod_partition")
             || !strcmp(mod->name, "mod_io")
             || !strcmp(mod->name, "mod_tcp")
             || !strcmp(mod->name, "mod_traffic")
             || !strcmp(mod->name, "mod_nginx"))
        {
            mod->enable = 1;

        } else {
            mod->enable = 0;
        }
    }
}
/*end*/
#endif

/*
 * 1. alloc or realloc store array
 * 2. set mod->n_item
 */
void
init_module_fields()
{
    int    i;
    struct module *mod = NULL;

    for (i = 0; i < statis.total_mod_num; i++) {
        mod = &mods[i];
        if (!mod->enable) {
            continue;
        }

        if (MERGE_ITEM == conf.print_merge) {
            mod->n_item = 1;

        } else {
            /* get mod->n_item first, and mod->n_item will be reseted in reading next line */
            mod->n_item = get_strtok_num(mod->record, ITEM_SPLIT);
        }

        if (mod->n_item) {
            mod->pre_array = (U_64 *)calloc(mod->n_item * mod->n_col, sizeof(U_64));
            mod->cur_array = (U_64 *)calloc(mod->n_item * mod->n_col, sizeof(U_64));
            mod->st_array = (double *)calloc(mod->n_item * mod->n_col, sizeof(double));
            if (conf.print_tail) {
                mod->max_array = (double *)calloc(mod->n_item * mod->n_col, sizeof(double));
                mod->mean_array = (double *)calloc(mod->n_item * mod->n_col, sizeof(double));
                mod->min_array = (double *)calloc(mod->n_item * mod->n_col, sizeof(double));
            }
        }
    }
}


/*
 * 1. realloc store array when mod->n_item is modify
 */
void
realloc_module_array(struct module *mod, int n_n_item)
{//分配存储空间，mod->n_col为一调数据包含的数字个数。n_n_item为这个模块包含多少条类似的数据。比如IO就有2条
    if (n_n_item > mod->n_item) {
        if (mod->pre_array) {
            mod->pre_array = (U_64 *)realloc(mod->pre_array, n_n_item * mod->n_col * sizeof(U_64));
            mod->cur_array = (U_64 *)realloc(mod->cur_array, n_n_item * mod->n_col * sizeof(U_64));
            mod->st_array = (double *)realloc(mod->st_array, n_n_item * mod->n_col * sizeof(double));
            if (conf.print_tail) {
                mod->max_array = (double *)realloc(mod->max_array, n_n_item * mod->n_col *sizeof(double));
                mod->mean_array =(double *)realloc(mod->mean_array, n_n_item * mod->n_col *sizeof(double));
                mod->min_array = (double *)realloc(mod->min_array, n_n_item * mod->n_col *sizeof(double));
            }

        } else {
            mod->pre_array = (U_64 *)calloc(n_n_item * mod->n_col, sizeof(U_64));
            mod->cur_array = (U_64 *)calloc(n_n_item * mod->n_col, sizeof(U_64));
            mod->st_array = (double *)calloc(n_n_item * mod->n_col, sizeof(double));
            if (conf.print_tail) {
                mod->max_array = (double *)calloc(n_n_item * mod->n_col, sizeof(double));
                mod->mean_array =(double *)calloc(n_n_item * mod->n_col, sizeof(double));
                mod->min_array = (double *)calloc(n_n_item * mod->n_col, sizeof(double));
            }
        }
    }
}

/*
 * set st result in st_array
 */
void
set_st_record(struct module *mod)
{//将cur_array的数据处理后，放到st_array以备打印。同时计算统计数据
    int    i, j, k = 0;
    struct mod_info *info = mod->info;

    mod->st_flag = 1;

    for (i = 0; i < mod->n_item; i++) {
        /* custom statis compute */
        if (mod->set_st_record) {//如果用户设置了"设置统计数据回调"，则先直接调用她。
            mod->set_st_record(mod, &mod->st_array[i * mod->n_col],
                    &mod->pre_array[i * mod->n_col],
                    &mod->cur_array[i * mod->n_col],
                    conf.print_interval);
        }

        for (j=0; j < mod->n_col; j++) {//对一条数据里面的每个数字，一个个处理
            if (!mod->set_st_record) {
                switch (info[j].stats_opt) {
                    case STATS_SUB://相减
                        if (mod->cur_array[k] < mod->pre_array[k]) {
                            mod->pre_array[k] = mod->cur_array[k];
                            mod->st_flag = 0;

                        } else {
                            mod->st_array[k] = mod->cur_array[k] - mod->pre_array[k];
                        }
                        break;
                    case STATS_SUB_INTER://相减求平均
                        if (mod->cur_array[k] < mod->pre_array[k]) {
                            mod->pre_array[k] = mod->cur_array[k];
                            mod->st_flag = 0;

                        } else {
                            mod->st_array[k] = (mod->cur_array[k] -mod->pre_array[k]) / conf.print_interval;
                        }
                        break;
                    default:
                        mod->st_array[k] = mod->cur_array[k];
                }
                mod->st_array[k] *= 1.0;
            }

            if (conf.print_tail) {
                if (0 == mod->n_record) {
                    mod->max_array[k] = mod->mean_array[k] = mod->min_array[k] = mod->st_array[k] * 1.0;

                } else {
                    if (mod->st_array[k] - mod->max_array[k] > 0.1) {
                        mod->max_array[k] = mod->st_array[k];
                    }
                    if (mod->min_array[k] - mod->st_array[k] > 0.1 && mod->st_array[k] >= 0) {
                        mod->min_array[k] = mod->st_array[k];
                    }
                    if (mod->st_array[k] >= 0) {
                        mod->mean_array[k] = ((mod->n_record - 1) *mod->mean_array[k] + mod->st_array[k]) / mod->n_record;
                    }
                }
            }
            k++;
        }
    }

    mod->n_record++;
}


/*
 * if diable = 1, then will disable module when record is null
 */
void
collect_record()
{
    int    i;
    struct module *mod = NULL;

    for (i = 0; i < statis.total_mod_num; i++) {
        mod = &mods[i];
        if (!mod->enable) {
            continue;
        }

        memset(mod->record, 0, sizeof(mod->record));//清空上次的数据
        if (mod->data_collect) {//调用其read_cpu_stats回调函数，获取数据。比如CPU的为read_cpu_stats
        //回调函数读取数据后，会将字符串数据放到mod->record里面去的。
            mod->data_collect(mod, mod->parameter);
        }
    }
}


/*
 * computer mod->st_array and swap cur_info to pre_info
 * return:  1 -> ok
 *      0 -> some mod->n_item have modify will reprint header
 */
int
collect_record_stat()
{//对从每个模块收集回来的字符串数据进行加工处理，将,逗号分开的数字处理后放入cur_array，
//然后进行统计，放入st_array以备打印。必要时统计方式会回调各个模块的回调。
    int    i, n_item, ret, no_p_hdr = 1;
    U_64  *tmp, array[MAX_COL_NUM] = {0};
    struct module *mod = NULL;

    for (i = 0; i < statis.total_mod_num; i++) {
        mod = &mods[i];
        if (!mod->enable) {
            continue;
        }

        memset(array, 0, sizeof(array));
        mod->st_flag = 0;
        ret = 0;

        if ((n_item = get_strtok_num(mod->record, ITEM_SPLIT))) {//得到item数目
            /* not merge mode, and last n_item != cur n_item, then reset mod->n_item and set reprint header flag */
            if (MERGE_ITEM != conf.print_merge && n_item && n_item != mod->n_item) {
                no_p_hdr = 0;//这个模块的列数发生变化了，需要重新打印头部，分配各个数组长度。
                /* reset struct module fields */
                realloc_module_array(mod, n_item);
            }

            mod->n_item = n_item;//item数目，比如I/O 分读写
            /* multiply item because of have ITEM_SPLIT */
            if (strstr(mod->record, ITEM_SPLIT)) {
                /* merge items */
                if (MERGE_ITEM == conf.print_merge) {
                    mod->n_item = 1;
                    ret = merge_mult_item_to_array(mod->cur_array, mod);

                } else {
                    char item[LEN_128] = {0};
                    int num = 0;
                    int pos = 0;
					//循环将当前模块的不同数据放到当前数组cur_array
                    while (strtok_next_item(item, mod->record, &pos)) {
						//将这一条数据里面逗号分隔的数字放到字cur_array对应的位置。
                        if (!(ret=convert_record_to_array(&mod->cur_array[num * mod->n_col], mod->n_col, item))) {
                            break;
                        }
                        memset(item, 0, sizeof(item));
                        num++;
                    }
                }

            } else { /* one item *///如果只有一个item，直接将其数据放入cur_array的开头即可。一共n_col个数。每个都为浮点数。
                ret = convert_record_to_array(mod->cur_array, mod->n_col, mod->record);
            }

            /* get st record */
            if (no_p_hdr && mod->pre_flag && ret) {//这是不是第一条数据，并且不是最后一条数据。就需要进行数据合并
                set_st_record(mod);///进行统计。合并数据，求和等。结果放入st_array
            }

            if (!ret) {
                mod->pre_flag = 0;

            } else {//刚才是成功得到一条数据的。下一条数据需要进行合并。
                mod->pre_flag = 1;
            }

        } else {
            mod->pre_flag = 0;
        }
        /* swap cur_array to pre_array */
        tmp = mod->pre_array;
        mod->pre_array = mod->cur_array;//pre_array指向新的，待会就是旧的数据了。其实这种代码放在最开头是比较自然的。
        mod->cur_array = tmp;//老的数据
    }

    return no_p_hdr;
}


/*
 * free module info
 */
void
free_modules()
{
    int    i;
    struct module *mod;

    for (i = 0; i < statis.total_mod_num; i++) {
        mod = &mods[i];
        if (mod->lib) {
            dlclose(mod->lib);
        }

        if (mod->cur_array) {
            free(mod->cur_array);
            mod->cur_array = NULL;
            free(mod->pre_array);
            mod->pre_array = NULL;
            free(mod->st_array);
            mod->st_array = NULL;
        }

        if (mod->max_array) {
            free(mod->max_array);
            free(mod->mean_array);
            free(mod->min_array);
            mod->max_array = NULL;
            mod->mean_array = NULL;
            mod->min_array = NULL;
        }
    }
}


/*
 * read line from file to mod->record
 */
void
read_line_to_module_record(char *line)
{
    int    i;
    struct module *mod;
    char  *s_token, *e_token;

    line[strlen(line) - 1] = '\0';
    for (i = 0; i < statis.total_mod_num; i++) {
        mod = &mods[i];
        if (mod->enable) {
            char mod_opt[LEN_64];
            sprintf(mod_opt, "%s%s%s", SECTION_SPLIT, mod->opt_line, STRING_SPLIT);
            memset(mod->record, 0, sizeof(mod->record));

            s_token = strstr(line, mod_opt);//查找--cpu等模块的位置，
            if (!s_token) {
                continue;
            }

            s_token += sizeof(SECTION_SPLIT) + strlen(mod->opt_line) + sizeof(STRING_SPLIT) - 2;
            e_token = strstr(s_token, SECTION_SPLIT);

            if (e_token) {
                memcpy(mod->record, s_token, e_token - s_token);

            } else {
                memcpy(mod->record, s_token, strlen(line) - (s_token - line));
            }
        }
    }
}


/*
 * if col num is zero then disable module
 */
void
disable_col_zero()
{//关闭那些n_col为0的模块，如果不是0，也就是输出的列数不为0.则需要
    int    i, j;
    struct module *mod = NULL;

    for (i = 0; i < statis.total_mod_num; i++) {
        mod = &mods[i];
        if (!mod->enable) {
            continue;
        }

        if (!mod->n_col) {
            mod->enable = 0;

        } else {
            int    p_col = 0;
            struct mod_info *info = mod->info;

            for (j = 0; j < mod->n_col; j++) {
                if (((DATA_SUMMARY == conf.print_mode) && (SUMMARY_BIT == info[j].summary_bit))
                        || ((DATA_DETAIL == conf.print_mode) && (HIDE_BIT != info[j].summary_bit))) {
                    p_col++;
                    break;
                }
            }

            if (!p_col) {
                mod->enable = 0;
            }
        }
    }
}
