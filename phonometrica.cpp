/***********************************************************************************************************************
*                                                                                                                      *
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
 * Created: 21/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: main application.                                                                                          *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifdef PHON_GUI
#include <clocale>
#include <QFile>
#include <QApplication>
#include <phon/gui/main_window.hpp>
#include <phon/application/settings.hpp>
#include <phon/application/project.hpp>
#else
#include <phon/runtime.hpp>
#endif

using namespace phonometrica;

static void show_usage()
{
	std::cout << "Usage: phonometrica [option] file" << std::endl;
	std::cout << "Options: " << std::endl;
	std::cout << " -l\t(list)\tlist bytecode (disassemble) file" << std::endl;
	std::cout << " -r\t(run)\texecute file" << std::endl;
	std::cout << " -a\t(all)\tdisassemble and execute file" << std::endl;
}

static void initialize(Runtime &rt)
{
#ifdef PHON_GUI
    // Set up the script loader from Qt resources.
    Settings::load_script = [](const String &name) -> String {
        auto qname = QString::fromUtf8(name.data(), (int) name.size());
        QFile file(QString(":/std/%1.phon").arg(qname));
        if (!file.open(QIODevice::ReadOnly))
        {
            throw error("Cannot load bundled script \"%\"", name);
        }
        auto bytes = file.readAll();
        return {bytes.constData(), bytes.size()};
    };

	rt["phon"] = make_handle<Module>(&rt, "phon");
	Settings::initialize(&rt);
	Settings::read();

	Project::preinitialize(rt);
	Project::create(rt);
	Project::initialize(rt);

	Sound::set_sound_formats();

	run_script(rt, initialize);
	run_script(rt, signal);
	run_script(rt, speech_analysis);
#endif // PHON_GUI
}

static void finalize(Runtime &)
{
#ifdef PHON_GUI
	Settings::write();
#endif
}

int main(int argc, char **argv)
{
	// If we have command-line arguments (beyond the program name), run in text mode.
	if (argc > 1)
	{
		Runtime runtime(argv[0]);
		runtime.set_text_mode(true);

		int error_code = 0;

		try
		{
			if (argc > 2)
			{
				String option(argv[1]), path(argv[2]);

				if (option == "-l")
				{
					auto closure = runtime.compile_file(path);
					runtime.disassemble(*closure, "main");
				}
				else if (option == "-r")
				{
					runtime.do_file(path);
				}
				else if (option == "-a")
				{
					auto closure = runtime.compile_file(path);
					runtime.disassemble(*closure, "main");
					puts("-------------------------------------------------------------------\n");
					runtime.interpret(closure);
				}
				else
				{
					show_usage();
					error_code = 1;
				}
			}
			else
			{
				String path(argv[1]);
				runtime.do_file(path);
			}
		}
		catch (RuntimeError &e)
		{
			utils::fprintf(stderr, "Error on line %:\n", e.line_no());
			utils::print(stderr, e.what());
			utils::print(stderr, "\n");
			error_code = 1;
		}
		catch (std::bad_alloc &)
		{
			utils::print(stderr, "out of memory error\n");
			error_code = 1;
		}
		catch (std::exception &e)
		{
			utils::print(stderr, e.what());
			utils::print(stderr, "\n");
			error_code = 1;
		}

		return error_code;
	}

	// No arguments: launch the GUI.
#ifdef PHON_GUI
#ifdef PHON_MACOS
	QApplication::setStyle("macOS");
#endif
	QApplication app(argc, argv);

	// QApplication sets LC_ALL to the system locale, which breaks strtod/sscanf
	// parsing of numbers with '.' as decimal separator (e.g. French locale uses ',').
	// Force LC_NUMERIC back to "C" so that pugixml's as_double(), strtod(), sscanf("%lf")
	// and all other C-level numeric parsing use '.' consistently.
	std::setlocale(LC_NUMERIC, "C");
	QApplication::setApplicationName("Phonometrica");
	QApplication::setOrganizationName("Phonometrica");
	QGuiApplication::setDesktopFileName("Phonometrica");
	app.setWindowIcon(QIcon(":/icons/phonometrica.svg"));

	Runtime runtime(argv[0]);
	runtime.set_text_mode(false);
	initialize(runtime);

	MainWindow window(runtime);
	window.show();

	Settings::post_initialize();
	window.postInitialize();

	int result = app.exec();
	finalize(runtime);
	return result;
#else
	show_usage();
	return 1;
#endif
}
