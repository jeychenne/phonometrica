#include <vector>
#include <utility>
#include <QString>

static std::vector<std::pair<const char*, std::vector<QString>>> function_declarations = {

	// ── Core type conversions ───────────────────────────────────
	{ "type",  {
		"type(object as Object)\nReturns the type of `object` as a string."
	}},
	{ "len",  {
		"len(file as File)\nReturns the number of bytes in the file.\002",
		"len(regex as Regex)\nReturns the number of captures in the last match.\001\002",
		"len(list as List)\nReturns the number of elements in the list.\001\002",
		"len(string as String)\nReturns the number of characters in the string.\001"
	}},
	{ "str",  {
		"str(object as Object)\nReturns a string representation of `object`."
	}},
	{ "bool",  {
		"bool(object as Object)\nConverts `object` to a boolean value."
	}},
	{ "int",  {
		"int(object as Object)\nConverts `object` to an integer value."
	}},
	{ "float",  {
		"float(object as Object)\nConverts `object` to a floating-point value."
	}},
	{ "import",  {
		"import(path as String)\nImport and execute a script file.\002",
		"import(path as String, once as Boolean)\nImport and execute a script file; if `once` is true, skip if already imported.\001"
	}},

	// ── JSON ────────────────────────────────────────────────────
	{ "load_json",  {
		"load_json(path as String)\nLoads a JSON file and returns it as a Table."
	}},
	{ "dump_json",  {
		"dump_json(table as Table)\nSerializes a Table as a JSON string."
	}},

	// ── Math functions ──────────────────────────────────────────
	{ "abs",  {
		"abs(x as Number)\nReturns the absolute value of `x`.\002",
		"abs(x as Array)\nReturns a copy of the array in which `abs` has been applied to each element.\001"
	}},
	{ "acos",  {
		"acos(x as Number)\nReturns the arccosine of `x`.\002",
		"acos(x as Array)\nReturns a copy of the array in which `acos` has been applied to each element.\001"
	}},
	{ "asin",  {
		"asin(x as Number)\nReturns the arcsine of `x`.\002",
		"asin(x as Array)\nReturns a copy of the array in which `asin` has been applied to each element.\001"
	}},
	{ "atan",  {
		"atan(x as Number)\nReturns the arctangent of `x`.\002",
		"atan(x as Array)\nReturns a copy of the array in which `atan` has been applied to each element.\001"
	}},
	{ "atan2",  {
		"atan2(y as Number, x as Number)\nReturns the four-quadrant inverse tangent of `y` and `x`."
	}},
	{ "ceil",  {
		"ceil(x as Number)\nReturns the smallest integer no smaller than `x`.\002",
		"ceil(x as Array)\nReturns a copy of the array in which `ceil` has been applied to each element.\001"
	}},
	{ "cos",  {
		"cos(x as Number)\nReturns the cosine of `x`.\002",
		"cos(x as Array)\nReturns a copy of the array in which `cos` has been applied to each element.\001"
	}},
	{ "exp",  {
		"exp(x as Number)\nReturns the exponential of `x`.\002",
		"exp(x as Array)\nReturns a copy of the array in which `exp` has been applied to each element.\001"
	}},
	{ "floor",  {
		"floor(x as Number)\nReturns the largest integer that is no larger than `x`.\002",
		"floor(x as Array)\nReturns a copy of the array in which `floor` has been applied to each element.\001"
	}},
	{ "log",  {
		"log(x as Number)\nReturns the natural logarithm of `x`.\002",
		"log(x as Array)\nReturns a copy of the array in which `log` has been applied to each element.\001"
	}},
	{ "log2",  {
		"log2(x as Number)\nReturns the logarithm of `x` in base 2.\002",
		"log2(x as Array)\nReturns a copy of the array in which `log2` has been applied to each element.\001"
	}},
	{ "log10",  {
		"log10(x as Number)\nReturns the logarithm of `x` in base 10.\002",
		"log10(x as Array)\nReturns a copy of the array in which `log10` has been applied to each element.\001"
	}},
	{ "max",  {
		"max(x as Number, y as Number)\nReturns the larger value between `x` and `y`.\002",
		"max(x as Array)\nReturns the maximum value in the array.\001"
	}},
	{ "min",  {
		"min(x as Number, y as Number)\nReturns the smaller value between `x` and `y`.\002",
		"min(x as Array)\nReturns the minimum value in the array.\001"
	}},
	{ "random",  {
		"random()\nReturns a pseudo-random value in the interval [0, 1[ according to a uniform distribution."
	}},
	{ "round",  {
		"round(x as Number)\nRounds `x` to the nearest integer.\002",
		"round(x as Number, n as Number)\nRounds `x` to `n` decimal places.\001\002",
		"round(x as Array)\nReturns a copy of the array in which `round` has been applied to each element.\001"
	}},
	{ "sin",  {
		"sin(x as Number)\nReturns the sine of `x`.\002",
		"sin(x as Array)\nReturns a copy of the array in which `sin` has been applied to each element.\001"
	}},
	{ "sqrt",  {
		"sqrt(x as Number)\nReturns the square root of `x`.\002",
		"sqrt(x as Array)\nReturns a copy of the array in which `sqrt` has been applied to each element.\001"
	}},
	{ "tan",  {
		"tan(x as Number)\nReturns the tangent of `x`.\002",
		"tan(x as Array)\nReturns a copy of the array in which `tan` has been applied to each element.\001"
	}},

	// ── Array creation ──────────────────────────────────────────
	{ "zeros",  {
		"zeros(n as Integer)\nCreates a one-dimensional array of size `n` filled with zeros.\002",
		"zeros(nrow as Integer, ncol as Integer)\nCreates a two-dimensional array of size `nrow` x `ncol` filled with zeros.\001"
	}},
	{ "ones",  {
		"ones(n as Integer)\nCreates a one-dimensional array of size `n` filled with ones.\002",
		"ones(nrow as Integer, ncol as Integer)\nCreates a two-dimensional array of size `nrow` x `ncol` filled with ones.\001"
	}},

	// ── String functions ────────────────────────────────────────
	{ "contains",  {
		"contains(table as Table, key as Object)\nReturns `true` if there is an element in the table whose key is equal to `key`, and `false` otherwise.\002",
		"contains(list as List, item as Object)\nReturns `true` if `item` is in the list and `false` otherwise.\001\002",
		"contains(string as String, substring as String)\nReturns `true` if `string` contains `substring`, and `false` otherwise.\001"
	}},
	{ "starts_with",  {
		"starts_with(string as String, prefix as String)\nReturns true if the string starts with `prefix`, and `false` otherwise."
	}},
	{ "ends_with",  {
		"ends_with(string as String, suffix as String)\nReturns true if the string ends with `suffix`, and `false` otherwise."
	}},
	{ "find",  {
		"find(list as List, item as Object)\nReturns the index of `item` in the list.\002",
		"find(list as List, item as Object, pos as Integer)\nReturns the index of `item` in the list, starting the search at index `pos`.\001\002",
		"find(string as String, substring as String)\nReturns the start position of `substring` in `string`, or 0 if it is not found.\001\002",
		"find(string as String, substring as String, pos as Integer)\nReturns the start position of `substring` in `string`, starting at `pos`.\001"
	}},
	{ "find_back",  {
		"find_back(list as List, item as Object)\nReturns the index of `item` in the list, starting the search from the end.\002",
		"find_back(string as String, substring as String)\nReturns the start position of `substring` in `string`, searching from the end.\001"
	}},
	{ "left",  {
		"left(string as String, n as Integer)\nGet the substring corresponding to the `n` first characters of the string."
	}},
	{ "right",  {
		"right(string as String, n as Integer)\nGet the substring corresponding to the `n` last characters of the string."
	}},
	{ "slice",  {
		"slice(string as String, from as Integer)\nReturns the substring starting at index `from` until the end of the string.\002",
		"slice(string as String, from as Integer, to as Integer)\nReturns the substring starting at index `from` and ending at index `to` (inclusive).\001"
	}},
	{ "count",  {
		"count(regex as Regex)\nReturns the number of captures in the last match.\002",
		"count(string as String, substring as String)\nReturns the number of times `substring` appears in `string`.\001"
	}},
	{ "to_upper",  {
		"to_upper(string as String)\nReturns a copy of the string where each character has been converted to upper case."
	}},
	{ "to_lower",  {
		"to_lower(string as String)\nReturns a copy of the string where each character has been converted to lower case."
	}},
	{ "char",  {
		"char(string as String, pos as Integer)\nGet character at position `pos`."
	}},
	{ "split",  {
		"split(string as String, delim as String)\nReturns a List of strings which have been split at each occurrence of the substring `delim`."
	}},
	{ "append",  {
		"append(ref list as List, item as Object)\nInserts `item` at the end of `list`.\002",
		"append(ref string as String, suffix as String)\nInserts `suffix` at the end of `string`.\001"
	}},
	{ "prepend",  {
		"prepend(ref list as List, item as Object)\nInserts `item` at the beginning of the list.\002",
		"prepend(ref string as String, prefix as String)\nInserts `prefix` at the beginning of `string`.\001"
	}},
	{ "insert",  {
		"insert(ref list as List, pos as Integer, item as Object)\nInserts the element `item` at index `pos`.\002",
		"insert(ref string as String, pos as Integer, sub as String)\nInserts the substring `sub` at position `pos`.\001"
	}},
	{ "trim",  {
		"trim(ref string as String)\nRemoves whitespace characters at both ends of the string."
	}},
	{ "ltrim",  {
		"ltrim(ref string as String)\nRemoves whitespace characters at the left end of the string."
	}},
	{ "rtrim",  {
		"rtrim(ref string as String)\nRemoves whitespace characters at the right end of the string."
	}},
	{ "remove",  {
		"remove(ref table as Table, key as Object)\nRemoves the element whose key is equal to `key`.\002",
		"remove(ref list as List, item as Object)\nRemoves all the elements in the list that are equal to `item`.\001\002",
		"remove(ref string as String, sub as String)\nRemoves all (non-overlapping) instances of the substring `sub`.\001"
	}},
	{ "remove_first",  {
		"remove_first(ref list as List, item as Object)\nRemoves the first element in the list that is equal to `item`.\002",
		"remove_first(ref string as String, sub as String)\nRemoves the first instance of the substring `sub`.\001"
	}},
	{ "remove_last",  {
		"remove_last(ref list as List, item as Object)\nRemoves the last element in the list that is equal to `item`.\002",
		"remove_last(ref string as String, sub as String)\nRemoves the last instance of the substring `sub`.\001"
	}},
	{ "remove_at",  {
		"remove_at(ref list as List, pos as Integer)\nRemoves the element at index `pos`.\002",
		"remove_at(ref string as String, at as Integer, count as Integer)\nRemoves `count` characters, starting at position `at`.\001"
	}},
	{ "replace",  {
		"replace(ref string as String, old as String, new as String)\nReplaces all (non-overlapping) instances of the substring `old` by `new`."
	}},
	{ "replace_first",  {
		"replace_first(ref string as String, old as String, new as String)\nReplaces the first instance of the substring `old` with `new`."
	}},
	{ "replace_last",  {
		"replace_last(ref string as String, old as String, new as String)\nReplaces the last instance of the substring `old` with `new`."
	}},
	{ "replace_at",  {
		"replace_at(ref string as String, at as Integer, count as Integer, new as String)\nReplaces `count` characters starting at position `at` with substring `new`."
	}},

	// ── List functions ──────────────────────────────────────────
	{ "first",  {
		"first(list as List)\nReturns the first element in the list."
	}},
	{ "last",  {
		"last(list as List)\nReturns the last element in the list."
	}},
	{ "join",  {
		"join(items as List, delim as String)\nReturns a string in which all the elements in `items` have been joined with the separator `delim`."
	}},
	{ "clear",  {
		"clear()\nClears the currently active output surface (console or Output panel).\002",
		"clear(ref table as Table)\nRemoves all the elements in the table.\002",
		"clear(ref list as List)\nEmpty the content of the list.\001"
	}},
	{ "is_empty",  {
		"is_empty(table as Table)\nReturns `true` if the table contains no element.\002",
		"is_empty(list as List)\nReturns `true` if the list is empty.\001\002",
		"is_empty(string as String)\nReturns `true` if the string is empty.\001"
	}},
	{ "pop",  {
		"pop(ref list as List)\nRemoves the last element from the list and returns it."
	}},
	{ "shift",  {
		"shift(ref list as List)\nRemoves the first element from the list and returns it."
	}},
	{ "sort",  {
		"sort(ref list as List)\nSorts the elements in the list in increasing order."
	}},
	{ "sorted_find",  {
		"sorted_find(list as List, item as Object)\nFinds the index of `item` in a sorted list."
	}},
	{ "sorted_insert",  {
		"sorted_insert(ref list as List, item as Object)\nInserts `item` into the sorted list at the correct position."
	}},
	{ "is_sorted",  {
		"is_sorted(list as List)\nReturns true if all the elements are sorted in ascending order."
	}},
	{ "reverse",  {
		"reverse(ref list as List)\nReverses the order of the elements in the list.\002",
		"reverse(ref string as String)\nReverses all characters in the string.\001"
	}},
	{ "sample",  {
		"sample(list as List, n as Integer)\nReturns a list containing `n` elements from the list drawn at random."
	}},
	{ "shuffle",  {
		"shuffle(ref list as List)\nRandomizes the order of the elements in the list."
	}},
	{ "intersect",  {
		"intersect(list1 as List, list2 as List)\nReturns a new list which contains all the elements that are in both lists."
	}},
	{ "unite",  {
		"unite(list1 as List, list2 as List)\nReturns a new list which contains all the elements that are in either list."
	}},
	{ "subtract",  {
		"subtract(list1 as List, list2 as List)\nReturns a new list which contains all the elements that are in `list1` but not in `list2`."
	}},

	// ── Table functions ─────────────────────────────────────────
	{ "get",  {
		"get(table as Table, key as Object)\nReturns the value associated with `key`, or `null` if there is no such value.\002",
		"get(table as Table, key as Object, default as Object)\nReturns the value associated with `key`, or `default` if there is no such value.\001"
	}},

	// ── File I/O ────────────────────────────────────────────────
	{ "open",  {
		"open(path as String)\nOpens the file named `path` and returns a handle to it.\002",
		"open(path as String, mode as String)\nOpens the file with the specified mode (\"r\", \"w\", \"a\").\001"
	}},
	{ "close",  {
		"close(file as File)\nCloses the file."
	}},
	{ "read",  {
		"read(file as File)\nReads and returns the entire content of the file as a string."
	}},
	{ "read_file",  {
		"read_file(path as String)\nReturn the content of the file named `path` as a string."
	}},
	{ "read_line",  {
		"read_line(file as File)\nReads a line from `file`."
	}},
	{ "read_lines",  {
		"read_lines(file as File)\nReturns the content of the file as a list whose elements are the lines of the file."
	}},
	{ "write",  {
		"write(file as File, text as String)\nWrites `text` to `file`."
	}},
	{ "write_line",  {
		"write_line(file as File, text as String)\nWrites `text` to `file`, and appends a new line separator."
	}},
	{ "write_lines",  {
		"write_lines(file as File, lines as List)\nWrites each string in `lines` to `file`, and appends a new line separator after each of them."
	}},
	{ "seek",  {
		"seek(file as File, pos as Integer)\nSets the position of the cursor in the file to `pos`."
	}},
	{ "tell",  {
		"tell(file as File)\nReturns the current position of the cursor in the file."
	}},
	{ "rewind",  {
		"rewind(file as File)\nRewinds the cursor to the beginning of the file."
	}},
	{ "eof",  {
		"eof(file as File)\nReturns `true` if the cursor is positioned at the end of the file."
	}},

	// ── Regex ───────────────────────────────────────────────────
	{ "match",  {
		"match(regex as Regex, subject as String)\nMatch `regex` against the string `subject`.\002",
		"match(regex as Regex, subject as String, pos as Integer)\nMatch `regex` against `subject`, starting at position `pos`.\001"
	}},
	{ "has_match",  {
		"has_match(regex as Regex)\nReturns `true` if the last call to `match` was successful."
	}},
	{ "group",  {
		"group(regex as Regex, nth as Integer)\nReturns the `nth` captured sub-expression in the last successful call to `match`."
	}},
	{ "get_start",  {
		"get_start(regex as Regex, nth as Integer)\nReturns the index of the first character of the `nth` capture in `regex`."
	}},
	{ "get_end",  {
		"get_end(regex as Regex, nth as Integer)\nReturns the index of the last character of the `nth` capture in `regex`."
	}},

	// ── File system ─────────────────────────────────────────────
	{ "exists",  {
		"exists(path as String)\nReturn `true` if the path exists, `false` otherwise."
	}},
	{ "is_document",  {
		"is_document(path as String)\nReturn `true` if `path` exists and is a file."
	}},
	{ "is_directory",  {
		"is_directory(path as String)\nReturn `true` if `path` exists and is a directory."
	}},
	{ "list_directory",  {
		"list_directory(path as String)\nReturn a table containing the files in `path`.\002",
		"list_directory(path as String, include_hidden as Boolean)\nReturn a table containing the files in `path`.\001"
	}},
	{ "clear_directory",  {
		"clear_directory(path as String)\nEmpty the content of a directory."
	}},
	{ "create_directory",  {
		"create_directory(path as String)\nCreate a new directory at the specified path."
	}},
	{ "remove_directory",  {
		"remove_directory(path as String)\nRemoves an empty directory.\002",
		"remove_directory(path as String, recursive as Boolean)\nRemoves a directory; if `recursive` is true, removes all contents.\001"
	}},
	{ "remove_file",  {
		"remove_file(path as String)\nRemoves a file."
	}},
	{ "remove_path",  {
		"remove_path(path as String)\nRemoves a file or directory."
	}},
	{ "rename",  {
		"rename(old_name as String, new_name as String)\nRenames a file."
	}},
	{ "get_extension",  {
		"get_extension(path as String)\nGet the file's extension, starting with a dot."
	}},
	{ "strip_extension",  {
		"strip_extension(path as String)\nReturn `path` without extension."
	}},
	{ "split_extension",  {
		"split_extension(path as String)\nReturn a table whose first element is `path` with the extension removed, and whose second element is the extension."
	}},
	{ "join_path",  {
		"join_path(s1 as String, s2 as String)\nConcatenate `s1` and `s2` using the native path separator."
	}},
	{ "genericize",  {
		"genericize(path as String)\nOn Windows, converts the native path separator to the generic separator `/`."
	}},
	{ "nativize",  {
		"nativize(path as String)\nOn Windows, converts the generic path separator to the native separator `\\`."
	}},
	{ "get_path_separator",  {
		"get_path_separator()\nGet the native path separator on the current platform."
	}},
	{ "get_user_directory",  {
		"get_user_directory()\nReturns the path of the user's home directory."
	}},
	{ "get_current_directory",  {
		"get_current_directory()\nReturns the current working directory."
	}},
	{ "get_script_path",  {
		"get_script_path()\nReturns the absolute path of the script file currently being interpreted. Use `get_directory(get_script_path())` to obtain the directory containing the script, and `join_path(...)` to build sibling paths portably."
	}},
	{ "set_current_directory",  {
		"set_current_directory(path as String)\nSets the current working directory to `path`."
	}},
	{ "get_temp_directory",  {
		"get_temp_directory()\nReturns the path of the system's temporary directory."
	}},
	{ "get_temp_name",  {
		"get_temp_name()\nReturns a unique temporary file name."
	}},
	{ "get_os_name",  {
		"get_os_name()\nReturns the name of the operating system."
	}},
	{ "get_full_path",  {
		"get_full_path(path as String)\nReturns the absolute path for the given path."
	}},
	{ "get_base_name",  {
		"get_base_name(path as String)\nReturns the file name component of the path."
	}},
	{ "get_directory",  {
		"get_directory(path as String)\nReturns the directory component of the path."
	}},
	{ "get_settings_directory",  {
		"get_settings_directory()\nReturns the path of the application settings directory."
	}},
	{ "get_metadata_directory",  {
		"get_metadata_directory()\nReturns the path of the metadata directory."
	}},
	{ "get_plugin_directory",  {
		"get_plugin_directory()\nReturns the path of the plugin directory."
	}},
	{ "get_script_directory",  {
		"get_script_directory()\nReturns the path of the standard scripts directory."
	}},
	{ "get_config_path",  {
		"get_config_path()\nReturns the path to the application configuration file."
	}},

	// ── Project & document access ───────────────────────────────
	{ "load",  {
		"load(path as String)\nImports the file into the project (if not already present) and returns it as a Document."
	}},
	{ "get_annotations",  {
		"get_annotations()\nReturn a list of all the annotations in the current project."
	}},
	{ "get_annotation",  {
		"get_annotation(path as String)\nReturn the Annotation object from the current project whose path is `path`, or `null`."
	}},
	{ "get_sounds",  {
		"get_sounds()\nReturn a list of all the sounds in the current project."
	}},
	{ "get_sound",  {
		"get_sound(path as String)\nReturn the Sound object from the current project whose path is `path`, or `null`."
	}},
	{ "get_concordances",  {
		"get_concordances()\nReturn a list of all the concordances in the current project."
	}},
	{ "get_concordance",  {
		"get_concordance(path as String)\nReturn the Concordance object from the current project whose path is `path`, or `null`."
	}},
	{ "get_datasets",  {
		"get_datasets()\nReturn a list of all the datasets in the current project."
	}},
	{ "get_dataset",  {
		"get_dataset(path as String)\nReturn the Dataset object from the current project whose path is `path`, or `null`."
	}},

	// ── Properties ──────────────────────────────────────────────
	{ "add_property",  {
		"add_property(doc as Document, category as String, value)\nAdds a metadata property with the given category and value to `doc`."
	}},
	{ "remove_property",  {
		"remove_property(doc as Document, category as String)\nRemoves the metadata property with the given category from `doc`."
	}},
	{ "get_property",  {
		"get_property(doc as Document, category as String)\nReturns the value of the metadata property with the given category."
	}},

	// ── Annotation ──────────────────────────────────────────────
	{ "bind_to_sound",  {
		"bind_to_sound(annot as Annotation, path as String)\nBinds the annotation to the sound file at `path`."
	}},
	{ "get_layer_count",  {
		"get_layer_count(annot as Annotation)\nReturns the number of annotation layers."
	}},
	{ "get_layer_label",  {
		"get_layer_label(annot as Annotation, layer as Integer)\nReturns the label of the given layer."
	}},
	{ "set_layer_label",  {
		"set_layer_label(annot as Annotation, layer as Integer, label as String)\nSets the label of the given layer."
	}},
	{ "get_event_count",  {
		"get_event_count(annot as Annotation, layer as Integer)\nReturns the number of events on the given layer."
	}},
	{ "get_event_start",  {
		"get_event_start(annot as Annotation, layer as Integer, event as Integer)\nReturns the start time of the event."
	}},
	{ "get_event_end",  {
		"get_event_end(annot as Annotation, layer as Integer, event as Integer)\nReturns the end time of the event."
	}},
	{ "get_event_text",  {
		"get_event_text(annot as Annotation, layer as Integer, event as Integer)\nReturns the text label of the event."
	}},
	{ "set_event_text",  {
		"set_event_text(annot as Annotation, layer as Integer, event as Integer, text as String)\nSets the text label of the event."
	}},
	{ "get_event_index",  {
		"get_event_index(annot as Annotation, layer as Integer, time as Number)\nReturns the index of the event at the given time."
	}},
	{ "add_interval",  {
		"add_interval(annot as Annotation, layer as Integer, start as Number, end as Number, text as String)\nAdds an interval with the given boundaries and text on the specified layer."
	}},
	{ "add_instant",  {
		"add_instant(annot as Annotation, layer as Integer, time as Number, text as String)\nAdds a point event at the given time on the specified layer."
	}},
	{ "remove_interval",  {
		"remove_interval(annot as Annotation, layer as Integer, start as Number, end as Number)\nRemoves the interval with the given boundaries from the specified layer."
	}},
	{ "remove_events",  {
		"remove_events(annot as Annotation, layer as Integer)\nRemoves all events from the specified layer."
	}},
	{ "create_layer",  {
		"create_layer(annot as Annotation, index as Integer, name as String, has_instants as Boolean)\nCreates a new annotation layer at the given index. If `has_instants` is true, the layer\ncontains point events; otherwise, it contains intervals."
	}},
	{ "remove_layer",  {
		"remove_layer(annot as Annotation, index as Integer)\nRemoves the annotation layer at the given index."
	}},
	{ "clear_layer",  {
		"clear_layer(annot as Annotation, index as Integer)\nRemoves all events from the layer at the given index."
	}},
	{ "duplicate_layer",  {
		"duplicate_layer(annot as Annotation, index as Integer, new_index as Integer)\nDuplicates the layer at `index` and inserts the copy at `new_index`."
	}},
	{ "layer_has_instants",  {
		"layer_has_instants(annot as Annotation, index as Integer)\nReturns `true` if the layer contains point events, `false` if it contains intervals."
	}},
	{ "save",  {
		"save(annot as Annotation)\nSaves the annotation to disk in its current format."
	}},
	{ "write_as_native",  {
		"write_as_native(annot as Annotation)\nSaves the annotation in Phonometrica's native XML format.\002",
		"write_as_native(annot as Annotation, path as String)\nSaves the annotation in native format at the specified `path`.\001"
	}},
	{ "write_as_textgrid",  {
		"write_as_textgrid(annot as Annotation)\nExports the annotation as a Praat TextGrid file.\002",
		"write_as_textgrid(annot as Annotation, path as String)\nExports the annotation as a TextGrid at the specified `path`.\001"
	}},

	// ── Sound & acoustic measurement ────────────────────────────
	{ "get_pitch",  {
		"get_pitch(sound as Sound, channel as Integer, time as Number)\nReturns the F0 value (Hz) at the given time.\002",
		"get_pitch(sound as Sound, channel as Integer, time as Number, min_pitch as Number, max_pitch as Number)\nReturns the F0 value (Hz) with the specified pitch range.\001"
	}},
	{ "get_mean_pitch",  {
		"get_mean_pitch(sound as Sound, channel as Integer, t1 as Number, t2 as Number)\nReturns the mean F0 value (Hz) between `t1` and `t2`."
	}},
	{ "get_formants",  {
		"get_formants(sound as Sound, channel as Integer, time as Number)\nReturns an array of formant values (Hz) at the given time.\002",
		"get_formants(sound as Sound, channel as Integer, time as Number, nformant as Integer,\n             nyquist as Number, window as Number, lpc_order as Integer)\nReturns formant values with custom analysis parameters.\001"
	}},
	{ "get_intensity",  {
		"get_intensity(sound as Sound, channel as Integer, time as Number)\nReturns the intensity (dB) at the given time."
	}},
	{ "get_mean_intensity",  {
		"get_mean_intensity(sound as Sound, channel as Integer, t1 as Number, t2 as Number)\nReturns the mean intensity (dB) between `t1` and `t2`."
	}},

	// ── Frequency conversion ────────────────────────────────────
	{ "hertz_to_bark",  {
		"hertz_to_bark(f)\nConverts frequency `f` (in Hertz) to bark."
	}},
	{ "bark_to_hertz",  {
		"bark_to_hertz(z)\nConverts frequency `z` (in bark) to Hertz."
	}},
	{ "hertz_to_erb",  {
		"hertz_to_erb(f)\nConverts frequency `f` (in Hertz) to ERB units."
	}},
	{ "erb_to_hertz",  {
		"erb_to_hertz(e)\nConverts frequency `e` (in ERB units) to Hertz."
	}},
	{ "hertz_to_mel",  {
		"hertz_to_mel(f)\nConverts frequency `f` (in Hertz) to mel."
	}},
	{ "mel_to_hertz",  {
		"mel_to_hertz(mel)\nConverts frequency `mel` (in mel) to Hertz."
	}},
	{ "hertz_to_semitones",  {
		"hertz_to_semitones(f0 [, ref])\nConverts frequency `f0` (in Hertz) to semitones, using `ref` as a reference frequency."
	}},
	{ "semitones_to_hertz",  {
		"semitones_to_hertz(st [, ref])\nConverts the number of semitones `st` to Hertz, using `ref` as a reference frequency."
	}},

	// ── Spectrum & spectral moments ─────────────────────────────
	{ "get_spectrum",  {
		"get_spectrum(sound as Sound, channel as Integer, t1 as Number, t2 as Number)\nComputes an FFT spectrum from the sound between `t1` and `t2` and returns a Spectrum object."
	}},
	{ "get_spectral_moments",  {
		"get_spectral_moments(sound as Sound, channel as Integer, time as Number,\n                     window as Number, min_freq as Number, max_freq as Number)\nComputes the four spectral moments (centre of gravity, spread, skewness, kurtosis)\nat the given time. Returns a Table with keys \"cog\", \"spread\", \"skewness\", \"kurtosis\"."
	}},

	// ── Data table access ───────────────────────────────────────
	{ "get_cell",  {
		"get_cell(table as DataTable, row as Integer, col as Integer)\nReturns the value of the cell at row `row` and column `col` as a string."
	}},
	{ "set_cell",  {
		"set_cell(table as DataTable, row as Integer, col as Integer, value as String)\nSets the value of the cell at row `row` and column `col`."
	}},
	{ "get_header",  {
		"get_header(table as DataTable, col as Integer)\nReturns the header (column name) of column `col`."
	}},
	{ "get_column",  {
		"get_column(dataset as Dataset, col as Integer)\nReturns the values in column `col` as an Array (numeric) or a List (text)."
	}},
	{ "get_column_type",  {
		"get_column_type(dataset as Dataset, col as Integer)\nReturns the type of column `col`: \"numeric\", \"text\", or \"boolean\"."
	}},
	{ "to_csv",  {
		"to_csv(table as DataTable, path as String)\nExports the table to a CSV file at `path`.\002",
		"to_csv(table as DataTable, path as String, separator as String)\nExports the table to a delimited file with the specified separator.\001"
	}},
	{ "filter",  {
		"filter(table as DataTable, expression as String)\nReturns a new dataset containing only the rows that match the filter expression.\002",
		"filter(table as DataTable, expression as String, label as String)\nReturns a filtered dataset with the given label.\001"
	}},
	{ "mean",  {
		"mean(x as Array)\nReturns the mean of the array `x`.\002",
		"mean(x as Array, dim as Integer)\nReturns the means along dimension `dim` (1 = columns, 2 = rows).\001"
	}},
	{ "std",  {
		"std(x as Array)\nReturns the standard deviation of the array `x`.\002",
		"std(x as Array, dim as Integer)\nReturns the standard deviations along dimension `dim`.\001"
	}},
	{ "sum",  {
		"sum(x as Array)\nReturns the sum of the elements in the array `x`.\002",
		"sum(x as Array, dim as Integer)\nReturns the sums along dimension `dim`.\001"
	}},
	{ "vrc",  {
		"vrc(x as Array)\nReturns the sample variance of the array `x`."
	}},

	// ── Statistical modeling ────────────────────────────────────
	{ "fit",  {
		"fit(formula as String, data as DataTable)\nFits a frequentist statistical model (Gaussian family) to the data.\002",
		"fit(formula as String, data as DataTable, family as String)\nFits a frequentist model with the specified family (\"gaussian\", \"binomial\", \"poisson\", \"negbin\").\002",
		"fit(formula as String, data as DataTable, priors as Prior)\nFits a Bayesian model (Gaussian family) using INLA-style approximate inference.\002",
		"fit(formula as String, data as DataTable, family as String, priors as Prior)\nFits a Bayesian model with the specified family using INLA-style approximate inference.\001"
	}},
	{ "summarize",  {
		"summarize(model as Model)\nPrints a detailed summary of the fitted model."
	}},
	{ "get_coef",  {
		"get_coef(model as Model)\nReturns the coefficient table of the fitted model as an Array."
	}},
	{ "compare",  {
		"compare(model1 as Model, model2 as Model)\nCompares two models. Frequentist models are compared using a likelihood\nratio test; Bayesian models are compared using WAIC, LOO-IC and Bayes factors.\nBoth models must use the same estimation method."
	}},
	{ "emmeans",  {
		"emmeans(model as Model, factor as String)\nComputes and prints estimated marginal means for the given factor.\002",
		"emmeans(model as Model, factor as String, adjustment as String)\nComputes EMMs and pairwise contrasts with p-value adjustment\n(\"holm\", \"bonferroni\", or \"none\").\001"
	}},
	{ "emtrends",  {
		"emtrends(model as Model, factor as String, variable as String)\nEstimates the slope of `variable` at each level of `factor`.\002",
		"emtrends(model as Model, factor as String, variable as String, adjustment as String)\nEstimates slopes and computes pairwise contrasts of trends.\001"
	}},
	{ "test_residuals",  {
		"test_residuals(model as Model)\nComputes DHARMa-style simulation-based residual diagnostics (KS test,\ndispersion test, outlier test) and prints the results."
	}},

	// ── GUI convenience functions ───────────────────────────────
	{ "get_current_annotation",  {
		"get_current_annotation()\nReturn the Annotation object loaded in the current view, or `null`."
	}},
	{ "get_current_sound",  {
		"get_current_sound()\nReturn the Sound object loaded in the current view, or `null`."
	}},
	{ "get_window_duration",  {
		"get_window_duration()\nReturn the duration of the visible window in the current annotation or sound view."
	}},
	{ "get_selection_duration",  {
		"get_selection_duration()\nReturn the duration of the selection in the current view, or 0 if there is no selection."
	}},
	{ "get_visible_channels",  {
		"get_visible_channels()\nReturn a list of the visible channel indices in the current view."
	}},
	{ "report_intensity",  {
		"report_intensity(time as Number)\nDisplays the intensity at the given time in the current view."
	}},
	{ "report_pitch",  {
		"report_pitch(time as Number)\nDisplays the pitch at the given time in the current view."
	}},
	{ "report_formants",  {
		"report_formants(time as Number)\nDisplays the values of the visible formants at the given time in the current view."
	}},
	{ "report_mean_intensity",  {
		"report_mean_intensity(t1 as Number, t2 as Number)\nDisplays the mean intensity between `t1` and `t2` in the current view."
	}},
	{ "report_mean_pitch",  {
		"report_mean_pitch(t1 as Number, t2 as Number)\nDisplays the mean pitch between `t1` and `t2` in the current view."
	}},
	{ "report_mean_formants",  {
		"report_mean_formants(t1 as Number, t2 as Number)\nDisplays the mean formant values between `t1` and `t2` in the current view."
	}},

	// ── Dialogs ─────────────────────────────────────────────────
	{ "warning",  {
		"warning(message [, title])\nDisplays a warning dialog."
	}},
	{ "alert",  {
		"alert(message [, title])\nDisplays an error dialog."
	}},
	{ "info",  {
		"info(message [, title])\nDisplays an information dialog."
	}},
	{ "ask",  {
		"ask(message [, title])\nAsks a Yes/No question to the user."
	}},
	{ "get_input",  {
		"get_input(label, title, text)\nDisplays an input dialog whose title is `title` and whose informative text is `label`."
	}},
	{ "open_file_dialog",  {
		"open_file_dialog(message as String)\nDisplays a dialog that lets the user select a file."
	}},
	{ "open_files_dialog",  {
		"open_files_dialog(message as String)\nDisplays a dialog that lets the user select one or more files."
	}},
	{ "save_file_dialog",  {
		"save_file_dialog(message as String)\nDisplays a dialog that lets the user choose a path to save a file."
	}},
	{ "open_directory_dialog",  {
		"open_directory_dialog(message as String)\nDisplays a dialog that lets the user select a directory."
	}},
	{ "create_dialog",  {
		"create_dialog(title as String)\nCreates a custom dialog with the given title.\002",
		"create_dialog(spec as Table)\nCreates a custom dialog from a JSON specification table.\001"
	}},
	{ "view_text",  {
		"view_text(path [, title [, width]])\nOpens the plain text file `path` in a new dialog."
	}},
	{ "create_progress_dialog",  {
		"create_progress_dialog(message as String, title as String, count as Integer)\nCreate a progress dialog with the provided message and title, set up for `count` elements."
	}},
	{ "update_progress_dialog",  {
		"update_progress_dialog(value as Integer)\nUpdate the progress dialog to the provided `value`."
	}},
	{ "launch_browser",  {
		"launch_browser(url as String)\nOpens the specified URL in the user's default web browser."
	}},

	// ── Plugins ─────────────────────────────────────────────────
	{ "get_plugin_version",  {
		"get_plugin_version(name as String)\nReturns the version string of the installed plugin with the given name."
	}},
	{ "get_plugin_resource",  {
		"get_plugin_resource(plugin as String, resource as String)\nReturns the path to a resource file inside the specified plugin."
	}},

	// ── Signals (standard library) ──────────────────────────────
	{ "create_signal",  {
		"create_signal()\nCreate and return a new signal identifier (id) of type String."
	}},
	{ "connect",  {
		"connect(id as String, slot as Function)\nConnect signal `id` to function `slot`."
	}},
	{ "disconnect",  {
		"disconnect(id as String, slot as Function)\nDisconnect signal `id` from function `slot`."
	}},
	{ "emit",  {
		"emit(id as String, arg as Object)\nEmit signal `id` with an argument `arg`."
	}}
};
