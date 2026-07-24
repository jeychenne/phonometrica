// Phonometrica engine — example host application (architecture §15, the M8
// acceptance deliverable). Copyright (C) 2019-2026 Julien Eychenne. GPLv3.
//
// A small program that *embeds* the engine, exercising the whole M8 embedding
// surface end to end:
//
//   * the typed registration API — `rt.add_function(...)` over scalars, String, and
//     Handle<T>, and `rt.add_class<T>(...)` for a host C++ class (Counter);
//   * Variant conversions for reading results back into C++;
//   * the cross-thread channel API — `channel_try_receive` polled like a GUI event
//     loop while worker *script* threads produce results (the GUI stand-in).
//
// Usage:
//   phon_repl              interactive REPL (persistent session)
//   phon_repl <file.phon>  run a script file
//   phon_repl --workers    run the worker-thread demo

// Embed through the public forwarding surface (phon/*.hpp) where one exists.
#include <phon/runtime.hpp> // Runtime + the SyntaxError/RuntimeError an embedder catches
#include <phon/string.hpp>
// No public forwarder for these yet — reach into the engine directly.
#include <phon/engine/concurrency/channel.hpp>
#include <phon/engine/core/cell.hpp>
#include <phon/engine/core/handle.hpp>
#include <phon/engine/core/variant.hpp>
#include <phon/engine/object/class.hpp>
#include <phon/engine/vm/interpreter.hpp> // stringify

#include <cmath>
#include <iostream>
#include <string>

using namespace phonometrica;

namespace {

// --- a host-defined C++ reference class exposed to scripts --------------------

// A plain host C++ class — no engine machinery. The engine boxes it when exposed.
struct Counter
{
	int64_t value;

	explicit Counter(int64_t start) : value(start) {}
};

std::string show(const Variant &v)
{
	String s = stringify(v.value());
	return std::string(s.data(), static_cast<size_t>(s.size()));
}

std::string to_std(const String &s)
{
	return std::string(s.data(), static_cast<size_t>(s.size()));
}

// Register the host's own functions and classes on the shared engine.
void register_host_extensions(Runtime &rt)
{
	// Typed functions: scalars in/out, and a String return.
	rt.add_function("hypot", [](double a, double b) { return std::sqrt(a * a + b * b); });
	rt.add_function("host_name", [] { return String("phon example host"); });

	// A host C++ class with reference (identity) semantics. Instances come from the
	// factory below; mutating through a Handle<Counter> mutates the shared object.
	rt.add_class<Counter>("Counter", rt.get_class("Object"));
	rt.add_function("make_counter", [](int64_t start) { return Handle<Counter>::make(start); });
	// A registered class is reached by a plain reference — mutating through `Counter &`
	// changes the shared object (identity), just like in application C++ code.
	rt.add_function("bump", [](Counter &c) { return ++c.value; });
	rt.add_function("count", [](const Counter &c) { return c.value; });

	// Embedder-injected isolate global (design §11): scripts read `host_version`
	// bare, like a script-side `global var`.
	rt.add_global("host_version", Variant::make(String("phon example host 1.0")));
}

// --- run modes ----------------------------------------------------------------

int run_file(Runtime &rt, const std::string &path)
{
	try
	{
		// do_file gives the chunk its source file: imports resolve in the script's
		// own directory and get_script_path() works.
		rt.do_file(String(path));
	}
	catch (const SyntaxError &e)
	{
		std::cerr << e.what() << " (line " << e.line << ")\n";
		return 1;
	}
	catch (const RuntimeError &e)
	{
		std::cerr << to_std(e.message) << " (line " << e.line << ")\n";
		return 1;
	}
	catch (const std::exception &e)
	{
		std::cerr << "phon: " << e.what() << "\n";
		return 1;
	}
	return 0;
}

int run_repl(Runtime &rt)
{
	std::cout << "phon REPL — enter an expression, Ctrl-D to exit.\n"
	             "Try: hypot(3, 4)   |   var c = make_counter(10)   |   bump(c)\n";
	rt.set_interactive(true); // REPL leniencies (design §11): x = 5 auto-declares
	std::string line;
	std::cout << "phon> " << std::flush;
	while (std::getline(std::cin, line))
	{
		if (!line.empty())
		{
			try
			{
				Variant v = rt.do_string(String(line));
				if (!v.is_null())
					std::cout << show(v) << "\n";
			}
			catch (const SyntaxError &e)
			{
				std::cout << e.what() << " (line " << e.line << ")\n";
			}
			catch (const RuntimeError &e)
			{
				std::cout << to_std(e.message) << " (line " << e.line << ")\n";
			}
		}
		std::cout << "phon> " << std::flush;
	}
	std::cout << "\n";
	return 0;
}

// The GUI stand-in: script worker threads produce results onto a shared Channel; the
// host thread polls it with a timeout, exactly as a Qt event-loop slot would.
int run_worker_demo(Runtime &rt)
{
	std::cout << "worker demo: 3 script workers each send 4 results to a Channel;\n"
	             "the host thread polls it like a GUI event loop (non-blocking).\n\n";

	// The script spawns the workers and hands the Channel back to the host.
	static const char *kScript =
	    "function worker(id, out)\n"
	    "    for i = 1 to 4 do\n"
	    "        send(out, id * 10 + i)\n"
	    "    end\n"
	    "end\n"
	    "var results = Channel()\n"
	    "spawn worker(1, results)\n"
	    "spawn worker(2, results)\n"
	    "spawn worker(3, results)\n"
	    "results\n";

	Variant channel = rt.do_string(String(kScript));

	const int expected = 12; // 3 workers * 4 results
	int received = 0;
	long sum = 0;
	int idle_polls = 0;
	while (received < expected)
	{
		Variant item;
		if (channel_try_receive(channel.value(), 0.05, item))
		{
			++received;
			sum += static_cast<long>(item.to<double>());
			std::cout << "  received " << show(item) << "  (" << received << "/" << expected
			          << ")\n";
		}
		else
		{
			++idle_polls; // a real GUI would service other events on an empty poll
		}
	}
	std::cout << "\nall " << expected << " results received; sum = " << sum << " (expected 270)"
	          << ", " << idle_polls << " idle poll(s)\n";
	return sum == 270 ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
	Runtime rt;
	register_host_extensions(rt);

	if (argc >= 2)
	{
		std::string arg = argv[1];
		if (arg == "--workers" || arg == "--demo")
			return run_worker_demo(rt);
		return run_file(rt, arg);
	}
	return run_repl(rt);
}
