#include <vector>
#include <utility>
#include <QString>

static std::vector<std::pair<const char*, std::vector<QString>>> function_declarations = {

	// ── Core builtins ───────────────────────────────────────────
	{ "print",  {
		"print(arg1, arg2, ...)\nPrints its arguments (separated by a space) to the console, followed by a new line."
	}},
	{ "len",  {
		"len(list as List)\nReturns the number of elements in the list.\002",
		"len(string as String)\nReturns the number of characters in the string.\001\002",
		"len(table as Table)\nReturns the number of key/value pairs in the table.\001\002",
		"len(set as Set)\nReturns the number of elements in the set.\001"
	}},
	{ "to_string",  {
		"to_string(object as Object)\nReturns a string representation of `object`."
	}},
	{ "to_int",  {
		"to_int(string as String)\nParses `string` as an integer; raises an error if it is not a valid integer."
	}},
	{ "to_float",  {
		"to_float(string as String)\nParses `string` as a floating-point number; raises an error if it is not a valid number."
	}},
	{ "cast",  {
		"cast(object as Object, type as Class)\nChecked downcast: returns `object` viewed as `type`, or raises a type error."
	}},
	{ "assert",  {
		"assert(condition as Boolean)\nRaises an error if `condition` is false or null.\002",
		"assert(condition as Boolean, message as String)\nRaises an error with `message` if `condition` is false or null.\001"
	}},
	{ "freeze",  {
		"freeze(object as Object)\nMakes a string or array immutable and shareable across threads (zero-copy on send). Returns the object."
	}},
	{ "collect_garbage",  {
		"collect_garbage()\nForces a garbage-collection pass over cyclic references."
	}},

	// ── Concurrency ─────────────────────────────────────────────
	{ "Channel",  {
		"Channel()\nCreates a synchronous (unbuffered) channel for communication between spawned threads.\002",
		"Channel(capacity as Integer)\nCreates a channel with an internal buffer of `capacity` elements.\001"
	}},
	{ "send",  {
		"send(channel as Channel, value as Object)\nSends `value` on the channel, blocking if the channel is full."
	}},
	{ "receive",  {
		"receive(channel as Channel)\nReceives the next value from the channel, blocking until one is available."
	}},
	{ "wait",  {
		"wait(thread as Object)\nWaits for a thread started with `spawn` to finish; re-raises any error it raised."
	}},
	{ "parallel_map",  {
		"parallel_map(items as List, fn as Function)\nApplies `fn` to each element of `items` in parallel and returns the list of results."
	}},

	// ── JSON ────────────────────────────────────────────────────
	{ "to_json",  {
		"to_json(value as Object)\nSerializes `value` (lists, tables, strings, numbers, booleans, null) as a JSON string.\002",
		"to_json(value as Object, indent as Integer)\nSerializes `value` as a pretty-printed JSON string with the given indentation.\001"
	}},
	{ "from_json",  {
		"from_json(text as String)\nParses the JSON string `text` and returns the corresponding value. Never evaluates code."
	}},

	// ── Math functions ──────────────────────────────────────────
	{ "abs",  {
		"abs(x as Number)\nReturns the absolute value of `x`.\002",
		"abs(x as Array)\nReturns a copy of the array in which `abs` has been applied to each element.\001"
	}},
	{ "acos",  {
		"acos(x as Number)\nReturns the arccosine of `x`."
	}},
	{ "asin",  {
		"asin(x as Number)\nReturns the arcsine of `x`."
	}},
	{ "atan",  {
		"atan(x as Number)\nReturns the arctangent of `x`."
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
		"log2(x as Number)\nReturns the logarithm of `x` in base 2."
	}},
	{ "log10",  {
		"log10(x as Number)\nReturns the logarithm of `x` in base 10."
	}},
	{ "max",  {
		"max(x as Number, y as Number)\nReturns the larger value between `x` and `y`.\002",
		"max(x as Array)\nReturns the maximum value in the array (raises an error on an empty array).\001"
	}},
	{ "min",  {
		"min(x as Number, y as Number)\nReturns the smaller value between `x` and `y`.\002",
		"min(x as Array)\nReturns the minimum value in the array (raises an error on an empty array).\001"
	}},
	{ "random",  {
		"random()\nReturns a pseudo-random value in the interval [0, 1[ according to a uniform distribution."
	}},
	{ "round",  {
		"round(x as Number)\nRounds `x` to the nearest integer.\002",
		"round(x as Number, n as Integer)\nRounds `x` to `n` decimal places.\001"
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
		"tan(x as Number)\nReturns the tangent of `x`."
	}},

	// ── Array creation & inspection ─────────────────────────────
	{ "zeros",  {
		"zeros(n as Integer)\nCreates a one-dimensional array of size `n` filled with zeros.\002",
		"zeros(nrow as Integer, ncol as Integer)\nCreates a two-dimensional array of size `nrow` x `ncol` filled with zeros.\001"
	}},
	{ "ones",  {
		"ones(n as Integer)\nCreates a one-dimensional array of size `n` filled with ones.\002",
		"ones(nrow as Integer, ncol as Integer)\nCreates a two-dimensional array of size `nrow` x `ncol` filled with ones.\001"
	}},
	{ "nrow",  {
		"nrow(x as Array)\nReturns the number of rows in the array."
	}},
	{ "ncol",  {
		"ncol(x as Array)\nReturns the number of columns in the array (1 for a one-dimensional array)."
	}},
	{ "ndim",  {
		"ndim(x as Array)\nReturns the number of dimensions of the array."
	}},
	{ "sum",  {
		"sum(x as Array)\nReturns the sum of the elements in the array `x`.\002",
		"sum(x as Array, dim as Integer)\nReturns the sums along dimension `dim` (1 = columns, 2 = rows).\001"
	}},
	{ "mean",  {
		"mean(x as Array)\nReturns the mean of the array `x`.\002",
		"mean(x as Array, dim as Integer)\nReturns the means along dimension `dim` (1 = columns, 2 = rows).\001"
	}},
	{ "std",  {
		"std(x as Array)\nReturns the standard deviation of the array `x`.\002",
		"std(x as Array, dim as Integer)\nReturns the standard deviations along dimension `dim`.\001"
	}},
	{ "vrc",  {
		"vrc(x as Array)\nReturns the sample variance of the array `x`."
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
		"find(list as List, item as Object)\nReturns the index of `item` in the list, or 0 if it is not found.\002",
		"find(list as List, item as Object, pos as Integer)\nReturns the index of `item` in the list, starting the search at index `pos`.\001\002",
		"find(string as String, substring as String)\nReturns the start position of `substring` in `string`, or 0 if it is not found.\001\002",
		"find(string as String, substring as String, pos as Integer)\nReturns the start position of `substring` in `string`, starting at `pos`.\001"
	}},
	{ "find_back",  {
		"find_back(list as List, item as Object)\nReturns the index of `item` in the list, starting the search from the end."
	}},
	{ "left",  {
		"left(string as String, n as Integer)\nGet the substring corresponding to the `n` first characters of the string.\002",
		"left(list as List, n as Integer)\nReturns a new list containing the `n` first elements of the list.\001"
	}},
	{ "right",  {
		"right(string as String, n as Integer)\nGet the substring corresponding to the `n` last characters of the string.\002",
		"right(list as List, n as Integer)\nReturns a new list containing the `n` last elements of the list.\001"
	}},
	{ "slice",  {
		"slice(string as String, from as Integer)\nReturns the substring starting at index `from` until the end of the string.\002",
		"slice(string as String, from as Integer, to as Integer)\nReturns the substring starting at index `from` and ending at index `to` (inclusive).\001"
	}},
	{ "count",  {
		"count(string as String, substring as String)\nReturns the number of times `substring` appears in `string`."
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
		"insert(ref list as List, pos as Integer, item as Object)\nInserts the element `item` at index `pos`."
	}},
	{ "trim",  {
		"trim(ref string as String)\nRemoves whitespace characters at both ends of the string (in place)."
	}},
	{ "ltrim",  {
		"ltrim(ref string as String)\nRemoves whitespace characters at the left end of the string (in place)."
	}},
	{ "rtrim",  {
		"rtrim(ref string as String)\nRemoves whitespace characters at the right end of the string (in place)."
	}},
	{ "remove",  {
		"remove(ref table as Table, key as Object)\nRemoves the element whose key is equal to `key`.\002",
		"remove(ref list as List, item as Object)\nRemoves all the elements in the list that are equal to `item`.\001\002",
		"remove(ref string as String, sub as String)\nRemoves all (non-overlapping) instances of the substring `sub`.\001"
	}},
	{ "remove_first",  {
		"remove_first(ref list as List, item as Object)\nRemoves the first element in the list that is equal to `item`."
	}},
	{ "remove_last",  {
		"remove_last(ref list as List, item as Object)\nRemoves the last element in the list that is equal to `item`."
	}},
	{ "remove_at",  {
		"remove_at(ref list as List, pos as Integer)\nRemoves the element at index `pos`."
	}},
	{ "replace",  {
		"replace(ref string as String, old as String, new as String)\nReplaces all (non-overlapping) instances of the substring `old` by `new` (in place)."
	}},
	{ "reverse",  {
		"reverse(ref list as List)\nReverses the order of the elements in the list.\002",
		"reverse(ref string as String)\nReverses all characters in the string.\001"
	}},
	{ "is_empty",  {
		"is_empty(table as Table)\nReturns `true` if the table contains no element.\002",
		"is_empty(list as List)\nReturns `true` if the list is empty.\001\002",
		"is_empty(string as String)\nReturns `true` if the string is empty.\001"
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
		"clear(ref list as List)\nEmpty the content of the list.\002",
		"clear(ref table as Table)\nRemoves all the elements in the table.\001\002",
		"clear(ref x as Array)\nSets all the elements in the array to 0.\001"
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
		"sorted_find(list as List, item as Object)\nFinds the index of `item` in a sorted list, or 0 if it is not found."
	}},
	{ "sorted_insert",  {
		"sorted_insert(ref list as List, item as Object)\nInserts `item` into the sorted list at the correct position."
	}},
	{ "is_sorted",  {
		"is_sorted(list as List)\nReturns true if all the elements are sorted in ascending order."
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
	{ "keys",  {
		"keys(table as Table)\nReturns the keys of the table as a List (in unspecified order)."
	}},
	{ "values",  {
		"values(table as Table)\nReturns the values of the table as a List (in unspecified order)."
	}},

	// ── File I/O ────────────────────────────────────────────────
	{ "File",  {
		"File(path as String)\nOpens the file named `path` for reading and returns a File handle.\002",
		"File(path as String, mode as String)\nOpens the file with the specified mode (\"r\", \"w\", \"a\", ...).\001\002",
		"File(path as String, mode as String, encoding as String)\nOpens the file with the specified mode and text encoding.\001"
	}},
	{ "open_file",  {
		"open_file(path as String)\nAlias for `File(path)`: opens the file for reading and returns a File handle.\002",
		"open_file(path as String, mode as String)\nAlias for `File(path, mode)`.\001\002",
		"open_file(path as String, mode as String, encoding as String)\nAlias for `File(path, mode, encoding)`.\001"
	}},
	{ "close",  {
		"close(file as File)\nCloses the file."
	}},
	{ "read",  {
		"read(file as File)\nReads and returns the entire content of the file as a string."
	}},
	{ "read_file",  {
		"read_file(path as String)\nReturn the content of the file named `path` as a string (the encoding is auto-detected)."
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
	{ "encoding",  {
		"encoding(file as File)\nReturns the file's text encoding, as detected or forced when the file was opened."
	}},

	// ── Regular expressions ─────────────────────────────────────
	{ "Regex",  {
		"Regex(pattern as String)\nCompiles `pattern` and returns a Regex object.\002",
		"Regex(pattern as String, flags as String)\nCompiles `pattern` with the given flags (e.g. \"i\" for case-insensitive).\001"
	}},
	{ "match",  {
		"match(re as Regex, subject as String)\nMatches `re` against `subject`; returns a Match object, or `null` if there is no match.\002",
		"match(re as Regex, subject as String, pos as Integer)\nMatches `re` against `subject`, starting at position `pos`.\001"
	}},
	{ "group",  {
		"group(m as Match, nth as Integer)\nReturns the `nth` captured group in the match (group 0 is the whole match), or `null` for a non-participating group."
	}},
	{ "group_count",  {
		"group_count(m as Match)\nReturns the number of groups in the match, including group 0 (the whole match)."
	}},
	{ "group_start",  {
		"group_start(m as Match, nth as Integer)\nReturns the index of the first character of the `nth` group in the subject string."
	}},
	{ "group_end",  {
		"group_end(m as Match, nth as Integer)\nReturns the index of the last character of the `nth` group in the subject string."
	}},
	{ "groups",  {
		"groups(m as Match)\nReturns all the captured groups in the match as a List."
	}},
	{ "pattern",  {
		"pattern(re as Regex)\nReturns the pattern string the regular expression was compiled from."
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
		"list_directory(path as String)\nReturn a list containing the names of the files in `path`.\002",
		"list_directory(path as String, include_hidden as Boolean)\nReturn a list containing the names of the files in `path`, optionally including hidden files.\001"
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
		"get_extension(path as String)\nGet the file's extension, starting with a dot.\002",
		"get_extension(path as String, lower as Boolean)\nGet the file's extension; if `lower` is true, it is converted to lower case.\001"
	}},
	{ "strip_extension",  {
		"strip_extension(path as String)\nReturn `path` without extension."
	}},
	{ "split_extension",  {
		"split_extension(path as String)\nReturn a list whose first element is `path` with the extension removed, and whose second element is the extension."
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
	{ "set_current_directory",  {
		"set_current_directory(path as String)\nSets the current working directory to `path`."
	}},
	{ "get_script_path",  {
		"get_script_path()\nReturns the absolute path of the script file currently being interpreted. Use `get_directory(get_script_path())` to obtain the directory containing the script, and `join_path(...)` to build sibling paths portably."
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
		"add_property(doc as Document, category as String, value as Object)\nAdds a metadata property with the given category and value (String, Number or Boolean) to `doc`."
	}},
	{ "remove_property",  {
		"remove_property(doc as Document, category as String)\nRemoves the metadata property with the given category from `doc`."
	}},
	{ "get_property",  {
		"get_property(doc as Document, category as String)\nReturns the value of the metadata property with the given category, or `null`."
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
	{ "new_annotation",  {
		"new_annotation()\nCreates and returns a new, empty Annotation."
	}},
	{ "duplicate_annotation",  {
		"duplicate_annotation(annot as Annotation, path as String)\nDuplicates the annotation and saves the copy at `path`; returns the new Annotation."
	}},
	{ "extract_layers",  {
		"extract_layers(annot as Annotation, layers as List, path as String)\nCreates a new annotation at `path` containing only the given layers (a list of indices)."
	}},
	{ "merge_annotations",  {
		"merge_annotations(base as Annotation, others as List, path as String)\nMerges the layers of `base` and of the annotations in `others` into a new annotation saved at `path`."
	}},
	{ "extract_annotation_slice",  {
		"extract_annotation_slice(annot as Annotation, t1 as Number, t2 as Number, path as String)\nExtracts the portion of the annotation between `t1` and `t2` into a new annotation saved at `path`.\002",
		"extract_annotation_slice(annot as Annotation, t1 as Number, t2 as Number, clip as Boolean, path as String)\nExtracts a slice of the annotation; if `clip` is true, events straddling the boundaries are clipped.\001"
	}},
	{ "concatenate_annotations",  {
		"concatenate_annotations(items as List, path as String)\nConcatenates the annotations in `items` (in order) into a new annotation saved at `path`.\002",
		"concatenate_annotations(items as List, durations as List, path as String)\nConcatenates the annotations, using `durations` for the duration of each source.\001"
	}},

	// ── Sound & acoustic measurement ────────────────────────────
	{ "get_pitch",  {
		"get_pitch(sound as Sound, channel as Integer, time as Number)\nReturns the F0 value (Hz) at the given time, using the global pitch-tracking settings.\002",
		"get_pitch(sound as Sound, channel as Integer, time as Number, options as Table)\nReturns the F0 value (Hz). Options keys (any subset, named-argument syntax supported): \"method\", \"min_pitch\", \"max_pitch\", \"threshold\", \"octave_jump_cost\", \"voicing_cost\", \"silence_threshold\", \"octave_cost\", \"use_gaussian\". Unspecified keys fall back to the global pitch-tracking settings.\001"
	}},
	{ "get_mean_pitch",  {
		"get_mean_pitch(sound as Sound, channel as Integer, t1 as Number, t2 as Number)\nReturns the mean F0 value (Hz) between `t1` and `t2`, using the global pitch-tracking settings.\002",
		"get_mean_pitch(sound as Sound, channel as Integer, t1 as Number, t2 as Number, options as Table)\nReturns the mean F0 value (Hz). Options keys (any subset, named-argument syntax supported): \"method\", \"min_pitch\", \"max_pitch\", \"threshold\", \"octave_jump_cost\", \"voicing_cost\", \"silence_threshold\", \"octave_cost\", \"time_step\", \"use_gaussian\". Unspecified keys fall back to the global pitch-tracking settings.\001"
	}},
	{ "get_formants",  {
		"get_formants(sound as Sound, channel as Integer, time as Number)\nReturns an array of formant values (Hz) at the given time, using the global formant settings.\002",
		"get_formants(sound as Sound, channel as Integer, time as Number, options as Table)\nReturns formant values at the given time. Options keys (any subset, named-argument syntax supported): \"nformant\", \"nyquist\", \"window_size\", \"lpc_order\". Unspecified keys fall back to the global formant settings.\001"
	}},
	{ "get_voice_report",  {
		"get_voice_report(sound as Sound, channel as Integer, t1 as Number, t2 as Number)\nReturns a Table with jitter, shimmer, HNR and pulse-summary measurements over [t1, t2), using Praat's voice-report F0 defaults (75–600 Hz).\002",
		"get_voice_report(sound as Sound, channel as Integer, t1 as Number, t2 as Number, options as Table)\nReturns a Table with voice-quality measurements. Options keys (any subset, named-argument syntax supported): \"f0_min\", \"f0_max\".\001"
	}},
	{ "get_intensity",  {
		"get_intensity(sound as Sound, channel as Integer, time as Number)\nReturns the intensity (dB) at the given time."
	}},
	{ "get_mean_intensity",  {
		"get_mean_intensity(sound as Sound, channel as Integer, t1 as Number, t2 as Number)\nReturns the mean intensity (dB) between `t1` and `t2`."
	}},
	{ "extract_sound_slice",  {
		"extract_sound_slice(sound as Sound, t1 as Number, t2 as Number, path as String)\nExtracts the portion of the sound between `t1` and `t2` into a new sound file saved at `path`."
	}},
	{ "concatenate_sounds",  {
		"concatenate_sounds(items as List, path as String)\nConcatenates the sounds in `items` (in order) into a new sound file saved at `path`."
	}},
	{ "convert",  {
		"convert(sound as Sound, path as String, format as String)\nConverts the sound to the given format (e.g. \"wav\", \"flac\") and saves it at `path`.\002",
		"convert(sound as Sound, path as String, format as String, sample_rate as Number)\nConverts the sound to the given format and sample rate, and saves it at `path`.\001"
	}},

	// ── Frequency conversion ────────────────────────────────────
	{ "hertz_to_bark",  {
		"hertz_to_bark(f as Number)\nConverts frequency `f` (in Hertz) to bark.\002",
		"hertz_to_bark(f as Array)\nConverts an array of frequencies (in Hertz) to bark.\001"
	}},
	{ "bark_to_hertz",  {
		"bark_to_hertz(z as Number)\nConverts frequency `z` (in bark) to Hertz.\002",
		"bark_to_hertz(z as Array)\nConverts an array of frequencies (in bark) to Hertz.\001"
	}},
	{ "hertz_to_erb",  {
		"hertz_to_erb(f as Number)\nConverts frequency `f` (in Hertz) to ERB units.\002",
		"hertz_to_erb(f as Array)\nConverts an array of frequencies (in Hertz) to ERB units.\001"
	}},
	{ "erb_to_hertz",  {
		"erb_to_hertz(e as Number)\nConverts frequency `e` (in ERB units) to Hertz.\002",
		"erb_to_hertz(e as Array)\nConverts an array of frequencies (in ERB units) to Hertz.\001"
	}},
	{ "hertz_to_mel",  {
		"hertz_to_mel(f as Number)\nConverts frequency `f` (in Hertz) to mel.\002",
		"hertz_to_mel(f as Array)\nConverts an array of frequencies (in Hertz) to mel.\001"
	}},
	{ "mel_to_hertz",  {
		"mel_to_hertz(mel as Number)\nConverts frequency `mel` (in mel) to Hertz.\002",
		"mel_to_hertz(mel as Array)\nConverts an array of frequencies (in mel) to Hertz.\001"
	}},
	{ "hertz_to_semitones",  {
		"hertz_to_semitones(f0 as Number)\nConverts frequency `f0` (in Hertz) to semitones, using the default reference frequency.\002",
		"hertz_to_semitones(f0 as Number, ref as Number)\nConverts frequency `f0` (in Hertz) to semitones, using `ref` as a reference frequency.\001"
	}},
	{ "semitones_to_hertz",  {
		"semitones_to_hertz(st as Number)\nConverts the number of semitones `st` to Hertz, using the default reference frequency.\002",
		"semitones_to_hertz(st as Number, ref as Number)\nConverts the number of semitones `st` to Hertz, using `ref` as a reference frequency.\001"
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
		"get_column(dataset as Dataset, col as Integer)\nReturns the values in column `col` as an Array (numeric) or a List (text).\002",
		"get_column(table as DataTable, name as String)\nReturns the values in the column named `name` as an Array (numeric) or a List (text).\001\002",
		"get_column(conc as Concordance, col as Integer)\nReturns the values in column `col` of the concordance.\001"
	}},
	{ "get_column_type",  {
		"get_column_type(dataset as Dataset, col as Integer)\nReturns the type of column `col`: \"numeric\", \"text\", or \"boolean\"."
	}},
	{ "add_column",  {
		"add_column(table as DataTable, values as List, name as String)\nAppends a new column named `name` with the given values to the table.\002",
		"add_column(table as DataTable, values as Array, name as String)\nAppends a new numeric column named `name` with the given values to the table.\001"
	}},
	{ "to_csv",  {
		"to_csv(table as DataTable, path as String)\nExports the table to a CSV file at `path`.\002",
		"to_csv(table as DataTable, path as String, separator as String)\nExports the table to a delimited file with the specified separator.\001"
	}},
	{ "filter",  {
		"filter(table as DataTable, expression as String)\nReturns a new dataset containing only the rows that match the filter expression.\002",
		"filter(table as DataTable, expression as String, label as String)\nReturns a filtered dataset with the given label.\001"
	}},

	// ── Statistical modeling ────────────────────────────────────
	{ "fit",  {
		"fit(formula as String, data as DataTable)\nFits a frequentist statistical model (Gaussian family) to the data.\002",
		"fit(formula as String, data as DataTable, family as String)\nFits a frequentist model with the specified family (\"gaussian\", \"binomial\", \"poisson\", \"negbin\").\002",
		"fit(formula as String, data as DataTable, options as Table)\nFits a frequentist Gaussian model with options.\nSupported options: fit_method=\"ML\" (default) or fit_method=\"REML\".\nREML applies only to mixed models with at least one random effect;\nit is silently coerced to ML otherwise.\002",
		"fit(formula as String, data as DataTable, family as String, options as Table)\nFits a frequentist model with the specified family and options.\nSupported options: fit_method=\"ML\" (default) or fit_method=\"REML\".\002",
		"fit(formula as String, data as DataTable, priors as PriorSpec)\nFits a Bayesian model (Gaussian family) using INLA-style approximate inference.\002",
		"fit(formula as String, data as DataTable, family as String, priors as PriorSpec)\nFits a Bayesian model with the specified family using INLA-style approximate inference.\001"
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
	{ "dharma",  {
		"dharma(model as Model)\nComputes DHARMa-style simulation-based residual diagnostics (KS test,\ndispersion test, outlier test) and prints the results."
	}},
	{ "evaluate",  {
		"evaluate(model as Model)\nEvaluates the model's Laplace objective at its converged parameters and returns\na Table with diagnostic quantities (log-likelihood components, random effects, ...).\002",
		"evaluate(model as Model, overrides as Table)\nEvaluates the model's objective with the given parameter overrides.\001"
	}},
	{ "polish",  {
		"polish(model as Model)\nRe-runs the Student-t outer optimization with tighter tolerances.\nReturns a Table with keys \"delta\", \"loglik\", \"ok\", \"message\". Only meaningful for Student-t models."
	}},
	{ "try_phase2",  {
		"try_phase2(model as Model)\nRe-fits the same Student-t model with Phase 2 (joint optimization) enabled.\nReturns a Table with keys \"ok\", \"delta\", \"loglik\", \"message\"."
	}},
	{ "predict",  {
		"predict(model as Model)\nComputes fitted values on the training data and returns them as a new Dataset.\002",
		"predict(model as Model, newdata as Dataset)\nComputes predictions for `newdata` and returns them as a new Dataset.\001\002",
		"predict(model as Model, newdata as Dataset, options as Table)\nComputes predictions for `newdata` with the given options.\001"
	}},
	{ "Prior",  {
		"Prior()\nCreates a PriorSpec with default priors, to be configured with `set_fixed`,\n`set_variance`, `set_residual`, etc., and passed to `fit` for Bayesian estimation."
	}},
	{ "set_fixed",  {
		"set_fixed(priors as PriorSpec, mean as Number, sd as Number)\nSets the Normal prior (mean, sd) for all fixed-effect coefficients.\002",
		"set_fixed(priors as PriorSpec, name as String, mean as Number, sd as Number)\nSets the Normal prior (mean, sd) for the fixed-effect coefficient named `name`.\001"
	}},
	{ "set_variance",  {
		"set_variance(priors as PriorSpec, type as String, param1 as Number)\nSets the prior on variance components: `type` is \"pc\", \"half_cauchy\" or \"half_normal\".\002",
		"set_variance(priors as PriorSpec, type as String, param1 as Number, param2 as Number)\nSets the prior on variance components with two parameters.\001"
	}},
	{ "set_residual",  {
		"set_residual(priors as PriorSpec, type as String, param1 as Number)\nSets the prior on the residual standard deviation: `type` is \"pc\", \"half_cauchy\" or \"half_normal\".\002",
		"set_residual(priors as PriorSpec, type as String, param1 as Number, param2 as Number)\nSets the prior on the residual standard deviation with two parameters.\001"
	}},
	{ "set_negbin_theta",  {
		"set_negbin_theta(priors as PriorSpec, shape as Number, rate as Number)\nSets the Gamma prior (shape, rate) on the negative-binomial dispersion parameter."
	}},
	{ "set_beta_phi",  {
		"set_beta_phi(priors as PriorSpec, shape as Number, rate as Number)\nSets the Gamma prior (shape, rate) on the Beta precision parameter."
	}},
	{ "set_lkj",  {
		"set_lkj(priors as PriorSpec, eta as Number)\nSets the LKJ prior (with strictly positive shape `eta`) on random-effect correlation matrices."
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

	// ── Dialogs ─────────────────────────────────────────────────
	{ "warning",  {
		"warning(message as String)\nDisplays a warning dialog.\002",
		"warning(message as String, title as String)\nDisplays a warning dialog with a custom title.\001"
	}},
	{ "alert",  {
		"alert(message as String)\nDisplays an error dialog.\002",
		"alert(message as String, title as String)\nDisplays an error dialog with a custom title.\001"
	}},
	{ "info",  {
		"info(message as String)\nDisplays an information dialog.\002",
		"info(message as String, title as String)\nDisplays an information dialog with a custom title.\001"
	}},
	{ "ask",  {
		"ask(message as String)\nAsks a Yes/No question to the user.\002",
		"ask(message as String, title as String)\nAsks a Yes/No question with a custom title.\001"
	}},
	{ "get_input",  {
		"get_input(label as String, title as String, text as String)\nDisplays an input dialog whose title is `title`, informative text is `label`, and initial value is `text`."
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
		"create_dialog(spec as String)\nCreates a custom dialog from a JSON specification string.\002",
		"create_dialog(spec as Table)\nCreates a custom dialog from a specification table.\001"
	}},
	{ "view_text",  {
		"view_text(path as String, title as String)\nOpens the plain text file `path` in a new dialog with the given title."
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
	{ "clear_console",  {
		"clear_console()\nClears the console."
	}},

	// ── Plugins ─────────────────────────────────────────────────
	{ "get_plugin_version",  {
		"get_plugin_version(name as String)\nReturns the version string of the installed plugin with the given name."
	}},
	{ "get_plugin_resource",  {
		"get_plugin_resource(plugin as String, resource as String)\nReturns the path to a resource file inside the specified plugin."
	}}
};
