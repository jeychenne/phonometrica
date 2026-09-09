/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any      *
 * later version.                                                                                                      *
 *                                                                                                                     *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied  *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more       *
 * details.                                                                                                            *
 *                                                                                                                     *
 * You should have received a copy of the GNU General Public License along with this program. If not, see              *
 * <http://www.gnu.org/licenses/>.                                                                                     *
 *                                                                                                                     *
 * Created: 28/02/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: read/write application settings. This class only has static methods.                                       *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SETTINGS_HPP
#define PHONOMETRICA_SETTINGS_HPP

#include <phon/runtime.hpp>
#include <phon/hashmap.hpp>
#include <phon/list.hpp>
#include <phon/table.hpp>

namespace phonometrica {

class Settings final
{
public:

	// `program_path` is the host executable's path (argv[0]). Windows and macOS
	// derive the bundled-resources directory from it; Linux uses a fixed prefix and
	// ignores it. It is a parameter rather than app-wide state so that the platforms
	// that need it cannot be left without it — the old engine's Runtime took argv[0]
	// in its constructor and offered program_path(); the new engine, being a
	// general-purpose embeddable language, has no business knowing either.
	static void initialize(Runtime *rt, const String &program_path);

	static void post_initialize();

    static String settings_directory();

    static String plugin_directory();

    static String metadata_directory();

    static String user_script_directory();

    static String resources_directory();

    static String config_path();

	static String get_string(const String &name);

	static String get_string(const String &category, const String &name);

	static bool get_boolean(const String &name);

	static bool get_boolean(const String &category, const String &name);

	static double get_number(const String &name);

	static double get_number(const String &category, const String &name);

	static int get_int(const String &name);

	static int get_int(const String &category, const String &name);

	// Returns the stored list BY VALUE (the engine List is CoW): mutating callers must
	// write the modified list back through set_value.
	static List get_list(const String &name);

	static void set_value(const String &key, Variant value);

	static void set_value(const String &key, Array<Variant> value);

	static void set_value(const String &category, const String &key, Variant value);

	// Convenience overloads boxing plain C++ values (String, bool, numbers, List, Table).
	static void set_value(const String &key, const char *value)
	{
		set_value(key, Variant::make(String(value)));
	}

	template<typename T>
	static void set_value(const String &key, T value)
	{
		set_value(key, Variant::make(std::move(value)));
	}

	static void set_value(const String &category, const String &key, const char *value)
	{
		set_value(category, key, Variant::make(String(value)));
	}

	template<typename T>
	static void set_value(const String &category, const String &key, T value)
	{
		set_value(category, key, Variant::make(std::move(value)));
	}

	static String get_std_plugin_directory();

    static String get_std_script(String name);

    static String get_last_directory();

    static void set_last_directory(const String &path);

    static void read();

    static void write();

    static String get_documentation_page(String page);

    static void reset();

    static void reset_mono_font();

    static void reset_waveform();

    static void reset_pitch_tracking();

    static void reset_formants();

    static void reset_spectrogram();

    static void reset_intensity();

    static void reset_autohints();

    static void reset_script_debug();

    static void reset_autoload();

    static void reset_autosave();

    static void reset_recent_views();

    static void reset_concordance();

    static void reset_geometry();

    static void reset_mouse_tracking();

    static void reset_sound_plots();

    static void reset_display();

    static void reset_statistics();

    static void reset_query();

    static void reset_whisper_log();

    static void reset_check_for_updates();

    static void reset_recording();

    static void reset_last_directory();

    static void reset_recent_projects();

    // Callback to load a bundled script by name. Set by the GUI layer.
    // The argument is a script name (e.g. "signal"), the return value is its content.
    static std::function<String(const String &)> load_script;

private:

	static Runtime *runtime;

	static String std_resource_path;

};

} // namespace phonometrica

#endif // PHONOMETRICA_SETTINGS_HPP
