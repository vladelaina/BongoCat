#include "cubism_model.hpp"

#include <cstring>
#include <yyjson.h>

namespace bongo_cat {
namespace {

bool safe_path_value(yyjson_val *value) {
    const char *path = yyjson_is_str(value) ? yyjson_get_str(value) : nullptr;
    if (!path || !path[0] || path[0] == '/' || path[0] == '\\' ||
        std::strchr(path, ':')) return false;
    const char *part = path;
    while (*part) {
        while (*part == '/' || *part == '\\') ++part;
        if (part[0] == '.' && part[1] == '.' &&
            (!part[2] || part[2] == '/' || part[2] == '\\')) return false;
        const char *next = std::strpbrk(part, "/\\");
        if (!next) break;
        part = next;
    }
    return true;
}

bool optional_path(yyjson_val *object, const char *key) {
    yyjson_val *value = yyjson_obj_get(object, key);
    return !value || (yyjson_is_str(value) && !yyjson_get_str(value)[0]) ||
        safe_path_value(value);
}

bool optional_number(yyjson_val *object, const char *key) {
    yyjson_val *value = yyjson_obj_get(object, key);
    return !value || yyjson_is_num(value);
}

bool valid_textures(yyjson_val *value) {
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) == 0 ||
        yyjson_arr_size(value) > 256) return false;
    size_t index, count; yyjson_val *item;
    yyjson_arr_foreach(value, index, count, item)
        if (!safe_path_value(item)) return false;
    return true;
}

bool valid_expressions(yyjson_val *value) {
    if (!value) return true;
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) > 1024) return false;
    size_t index, count; yyjson_val *item;
    yyjson_arr_foreach(value, index, count, item) {
        if (!yyjson_is_obj(item) || !yyjson_is_str(yyjson_obj_get(item, "Name")) ||
            !safe_path_value(yyjson_obj_get(item, "File"))) return false;
    }
    return true;
}

bool valid_motion(yyjson_val *item) {
    return yyjson_is_obj(item) && safe_path_value(yyjson_obj_get(item, "File")) &&
        optional_path(item, "Sound") && optional_number(item, "FadeInTime") &&
        optional_number(item, "FadeOutTime");
}

bool valid_motions(yyjson_val *value) {
    if (!value) return true;
    if (!yyjson_is_obj(value) || yyjson_obj_size(value) > 1024) return false;
    size_t index, maximum; yyjson_val *key, *group;
    yyjson_obj_foreach(value, index, maximum, key, group) {
        if (!yyjson_is_str(key) || !yyjson_is_arr(group) ||
            yyjson_arr_size(group) > 4096) return false;
        size_t item_index, count; yyjson_val *item;
        yyjson_arr_foreach(group, item_index, count, item)
            if (!valid_motion(item)) return false;
    }
    return true;
}

bool valid_groups(yyjson_val *value) {
    if (!value) return true;
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) > 1024) return false;
    size_t index, count; yyjson_val *group;
    yyjson_arr_foreach(value, index, count, group) {
        yyjson_val *ids = yyjson_obj_get(group, "Ids");
        if (!yyjson_is_obj(group) || !yyjson_is_str(yyjson_obj_get(group, "Target")) ||
            !yyjson_is_str(yyjson_obj_get(group, "Name")) || !yyjson_is_arr(ids) ||
            yyjson_arr_size(ids) > 4096) return false;
        size_t id_index, id_count; yyjson_val *id;
        yyjson_arr_foreach(ids, id_index, id_count, id)
            if (!yyjson_is_str(id)) return false;
    }
    return true;
}

bool valid_hit_areas(yyjson_val *value) {
    if (!value) return true;
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) > 1024) return false;
    size_t index, count; yyjson_val *item;
    yyjson_arr_foreach(value, index, count, item)
        if (!yyjson_is_obj(item) || !yyjson_is_str(yyjson_obj_get(item, "Id")) ||
            !yyjson_is_str(yyjson_obj_get(item, "Name"))) return false;
    return true;
}

bool valid_layout(yyjson_val *value) {
    if (!value) return true;
    if (!yyjson_is_obj(value) || yyjson_obj_size(value) > 64) return false;
    size_t index, maximum; yyjson_val *key, *item;
    yyjson_obj_foreach(value, index, maximum, key, item)
        if (!yyjson_is_str(key) || !yyjson_is_num(item)) return false;
    return true;
}

bool valid_references(yyjson_val *value) {
    return yyjson_is_obj(value) && safe_path_value(yyjson_obj_get(value, "Moc")) &&
        valid_textures(yyjson_obj_get(value, "Textures")) &&
        optional_path(value, "Physics") && optional_path(value, "Pose") &&
        optional_path(value, "DisplayInfo") && optional_path(value, "UserData") &&
        valid_expressions(yyjson_obj_get(value, "Expressions")) &&
        valid_motions(yyjson_obj_get(value, "Motions"));
}

} // namespace

bool validate_model_setting_json(const std::vector<unsigned char> &json,
    const char *setting_file, BongoCatError *error) {
    if (json.empty() || json.size() > 4 * 1024 * 1024) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Model setting is empty or too large: %s", setting_file ? setting_file : "");
        return false;
    }
    yyjson_read_err parse_error = {};
    yyjson_doc *document = yyjson_read_opts(
        reinterpret_cast<char *>(const_cast<unsigned char *>(json.data())),
        json.size(), 0, nullptr, &parse_error);
    yyjson_val *root = document ? yyjson_doc_get_root(document) : nullptr;
    bool valid = yyjson_is_obj(root) && yyjson_is_int(yyjson_obj_get(root, "Version")) &&
        yyjson_get_int(yyjson_obj_get(root, "Version")) == 3 &&
        valid_references(yyjson_obj_get(root, "FileReferences")) &&
        valid_groups(yyjson_obj_get(root, "Groups")) &&
        valid_hit_areas(yyjson_obj_get(root, "HitAreas")) &&
        valid_layout(yyjson_obj_get(root, "Layout"));
    if (document) yyjson_doc_free(document);
    if (valid) return true;
    bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
        "Invalid model setting JSON: %s (%s)", setting_file ? setting_file : "",
        parse_error.msg ? parse_error.msg : "unsupported model3 schema");
    return false;
}

} // namespace bongo_cat
