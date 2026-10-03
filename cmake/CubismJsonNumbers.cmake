function(bongo_cat_fix_cubism_json_numbers target)
  set(json_dir "${CUBISM_FRAMEWORK_PATH}/src/Utils")
  set(source_path "${json_dir}/CubismJson.cpp")
  set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/cubism-json")
  set(output_source "${output_dir}/CubismJson.cpp")
  file(READ "${source_path}" source)
  string(REPLACE "\r\n" "\n" source "${source}")

  set(begin_anchor "Value* CubismJson::ParseNumeric(")
  set(end_anchor "Value* CubismJson::ParseObject(")
  string(FIND "${source}" "${begin_anchor}" begin_position)
  string(FIND "${source}" "${end_anchor}" end_position)
  if(begin_position EQUAL -1 OR end_position LESS begin_position)
    message(FATAL_ERROR "Cubism 5 r.5 JSON patch mismatch: numeric parser")
  endif()
  string(SUBSTRING "${source}" 0 ${begin_position} prefix)
  string(SUBSTRING "${source}" ${end_position} -1 suffix)
  # Use the existing JSON dependency for locale-independent exponent parsing.
  # Copy only the bounded number token: SDK input need not be null-terminated.
  set(parser [=[Value* CubismJson::ParseNumeric(const csmChar* buffer, csmInt32 length, csmInt32 begin, csmInt32* outEndPos)
{
    if (_error)
    {
        return NULL;
    }
    if (!buffer || begin < 0 || begin >= length)
    {
        _error = "invalid numeric buffer";
        return NULL;
    }

    csmInt32 end = begin;
    for (; end < length; ++end)
    {
        const csmChar ch = buffer[end];
        if (ch == ',' || ch == ']' || ch == '}' ||
            ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
        {
            break;
        }
    }
    const std::string number(buffer + begin, static_cast<size_t>(end - begin));
    yyjson_val value;
    const char* parsedEnd = yyjson_read_number(number.c_str(), &value, 0, NULL, NULL);
    if (!parsedEnd || parsedEnd != number.c_str() + number.size())
    {
        _error = "invalid JSON number";
        return NULL;
    }
    *outEndPos = end;
    return CSM_NEW Float(static_cast<csmFloat32>(yyjson_get_num(&value)));
}


]=])
  set(source "${prefix}${parser}${suffix}")
  string(REPLACE "#include \"CubismJson.hpp\""
    "#include \"CubismJson.hpp\"\n#include <string>\n#include <yyjson.h>"
    source "${source}")
  file(MAKE_DIRECTORY "${output_dir}")
  file(WRITE "${output_source}" "${source}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source_path}")
  get_target_property(framework_sources ${target} SOURCES)
  list(REMOVE_ITEM framework_sources "${source_path}")
  set_property(TARGET ${target} PROPERTY SOURCES "${framework_sources}")
  target_sources(${target} PRIVATE "${output_source}")
  target_include_directories(${target} PRIVATE "${json_dir}")
  target_link_libraries(${target} PRIVATE yyjson)
endfunction()
