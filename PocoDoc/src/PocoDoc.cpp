//
// PocoDoc.cpp
//
// Copyright (c) 2005-2014, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// SPDX-License-Identifier:	BSL-1.0
//


#include "Poco/StringTokenizer.h"
#include "Poco/Glob.h"
#include "Poco/Path.h"
#include "Poco/File.h"
#include "Poco/DirectoryIterator.h"
#include "Poco/Process.h"
#include "Poco/Pipe.h"
#include "Poco/PipeStream.h"
#include "Poco/Environment.h"
#include "Poco/NumberFormatter.h"
#include "Poco/Exception.h"
#include "Poco/Stopwatch.h"
#include "Poco/DateTime.h"
#include "Poco/DateTimeFormatter.h"
#include "Poco/Timespan.h"
#include "Poco/DateTimeFormatter.h"
#include "Poco/Util/Application.h"
#include "Poco/Util/Option.h"
#include "Poco/Util/OptionSet.h"
#include "Poco/Util/HelpFormatter.h"
#include "Poco/Util/AbstractConfiguration.h"
#include "Poco/CppParser/Parser.h"
#include "Poco/CppParser/NameSpace.h"
#include "Poco/CppParser/Struct.h"
#include "Poco/CppParser/Utility.h"
#include "DocWriter.h"
#include <set>
#include <utility>
#include <memory>
#include <fstream>
#include <sstream>
#include <iostream>
#include <clocale>


using Poco::StringTokenizer;
using Poco::Glob;
using Poco::Path;
using Poco::File;
using Poco::DirectoryIterator;
using Poco::Process;
using Poco::ProcessHandle;
using Poco::Environment;
using Poco::NumberFormatter;
using Poco::Exception;
using Poco::Util::Application;
using Poco::Util::Option;
using Poco::Util::OptionSet;
using Poco::Util::OptionCallback;
using Poco::Util::HelpFormatter;
using Poco::Util::AbstractConfiguration;


class Preprocessor
{
public:
	Preprocessor(const ProcessHandle& proc, std::istream* pStream):
		_proc(proc),
		_pStream(pStream)
	{
	}

	Preprocessor(const ProcessHandle& proc, std::istream* pStream, const std::string& file):
		_proc(proc),
		_pStream(pStream),
		_file(file)
	{
	}

	std::istream& stream()
	{
		return *_pStream;
	}

	~Preprocessor()
	{
		int c = _pStream->get();
		while (c != -1) c = _pStream->get();
		delete _pStream;
		_proc.wait();
		if (!_file.empty())
		{
			try
			{
				File f(_file);
				f.remove();
			}
			catch (Exception&)
			{
			}
		}
	}

private:
	ProcessHandle _proc;
	std::istream* _pStream;
	std::string   _file;
};


class PocoDocApp: public Application
{
public:
	PocoDocApp():
		_helpRequested(false),
		_writeEclipseTOC(false),
		_searchIndexEnabled(false)
	{
		std::setlocale(LC_ALL, "");
	}

	~PocoDocApp()
	{
	}

protected:
	void initialize(Application& self)
	{
		loadConfiguration(); // load default configuration files, if present
		Application::initialize(self);
	}

	void uninitialize()
	{
		Application::uninitialize();
	}

	void reinitialize(Application& self)
	{
		Application::reinitialize(self);
	}

	void defineOptions(OptionSet& options)
	{
		Application::defineOptions(options);

		options.addOption(
			Option("help", "h", "Display help information on command line arguments.")
				.required(false)
				.repeatable(false)
				.callback(OptionCallback<PocoDocApp>(this, &PocoDocApp::handleHelp)));

		options.addOption(
			Option("config", "f", "Load configuration data from a file.")
				.required(false)
				.repeatable(true)
				.argument("file")
				.callback(OptionCallback<PocoDocApp>(this, &PocoDocApp::handleConfig)));

		options.addOption(
			Option("define", "D", "Define a configuration property.")
				.required(false)
				.repeatable(true)
				.argument("name=value")
				.callback(OptionCallback<PocoDocApp>(this, &PocoDocApp::handleDefine)));

		options.addOption(
			Option("eclipse", "e", "Write Eclipse TOC file.")
				.required(false)
				.repeatable(false)
				.callback(OptionCallback<PocoDocApp>(this, &PocoDocApp::handleEclipse)));

		options.addOption(
			Option("search-index", "s", "Enable search index (requires FTS5 support).")
				.required(false)
				.repeatable(false)
				.callback(OptionCallback<PocoDocApp>(this, &PocoDocApp::handleSearchIndex)));
	}

	void handleHelp(const std::string& name, const std::string& value)
	{
		_helpRequested = true;
		displayHelp();
		stopOptionsProcessing();
	}

	void handleDefine(const std::string& name, const std::string& value)
	{
		defineProperty(value);
	}

	void defineProperty(const std::string& def)
	{
		std::string name;
		std::string value;
		std::string::size_type pos = def.find('=');
		if (pos != std::string::npos)
		{
			name.assign(def, 0, pos);
			value.assign(def, pos + 1, def.length() - pos);
		}
		else name = def;
		config().setString(name, value);
	}

	void handleEclipse(const std::string& name, const std::string& value)
	{
		_writeEclipseTOC = true;
	}

	void handleSearchIndex(const std::string& name, const std::string& value)
	{
		_searchIndexEnabled = true;
	}

	void handleConfig(const std::string& name, const std::string& value)
	{
		loadConfiguration(value, -200);
	}

	void displayHelp()
	{
		HelpFormatter helpFormatter(options());
		helpFormatter.setCommand(commandName());
		helpFormatter.setUsage("OPTIONS");
		helpFormatter.setHeader("POCO C++ Libraries documentation builder.");
		helpFormatter.format(std::cout);
	}

	void buildFileList(std::set<std::string>& files)
	{
		std::set<std::string> temp;
		std::string includes = config().getString("PocoDoc.files.include");
		std::string excludes = config().getString("PocoDoc.files.exclude", "");
		StringTokenizer incTokenizer(includes, ",\n", StringTokenizer::TOK_IGNORE_EMPTY | StringTokenizer::TOK_TRIM);
		for (StringTokenizer::Iterator it = incTokenizer.begin(); it != incTokenizer.end(); ++it)
		{
			Glob::glob(*it, temp);
		}
		StringTokenizer excTokenizer(excludes, ",\n", StringTokenizer::TOK_IGNORE_EMPTY | StringTokenizer::TOK_TRIM);
		for (std::set<std::string>::const_iterator it = temp.begin(); it != temp.end(); ++it)
		{
			Path p(*it);
			bool include = true;
			for (StringTokenizer::Iterator itg = excTokenizer.begin(); itg != excTokenizer.end(); ++itg)
			{
				Glob glob(*itg);
				if (glob.match(p.getFileName()) || glob.match(p.toString()))
					include = false;
			}
			if (include)
				files.insert(*it);
		}
	}

	Preprocessor* preprocess(const std::string& file)
	{
		Path pp(file);
		pp.setExtension("i");
		std::string comp = "PocoDoc.compiler";
		std::string platformComp(comp);

		if (Environment::isWindows())
			platformComp += ".windows";
		else
			platformComp += ".unix";

		std::string exec = config().getString(platformComp + ".exec", config().getString(comp + ".exec", ""));
		std::string opts = config().getString(platformComp + ".options", config().getString(comp + ".options", ""));
		std::string path = config().getString(platformComp + ".path", config().getString(comp + ".path", ""));
		bool usePipe = config().getBool(platformComp + ".usePipe", config().getBool(comp + ".usePipe", false));

		std::string popts;
		for (std::string::const_iterator it = opts.begin(); it != opts.end(); ++it)
		{
			if (*it == '%')
				popts += pp.getBaseName();
			else
				popts += *it;
		}
		StringTokenizer tokenizer(popts, ",\n", StringTokenizer::TOK_IGNORE_EMPTY | StringTokenizer::TOK_TRIM);
		std::vector<std::string> args(tokenizer.begin(), tokenizer.end());
		args.push_back(file);

		if (!path.empty())
		{
			std::string newPath(Environment::get("PATH"));
			newPath += Path::pathSeparator();
			newPath += path;
			Environment::set("PATH", path);
		}

		if (usePipe)
		{
			Poco::Pipe inPipe;
			ProcessHandle proc = Process::launch(exec, args, 0, &inPipe, 0);
			return new Preprocessor(proc, new Poco::PipeInputStream(inPipe));
		}
		else
		{
			ProcessHandle proc = Process::launch(exec, args);
			proc.wait();
			return new Preprocessor(proc, new std::ifstream(pp.getFileName().c_str()), pp.getFileName());
		}
	}

	void parse(const std::string& file)
	{
		logger().information("Preprocessing " + file);
		std::unique_ptr<Preprocessor> pPreProc(preprocess(file));
		logger().information("Parsing " + file);
		if (pPreProc->stream().good())
		{
			Poco::CppParser::Parser parser(_gst, file, pPreProc->stream());
			parser.parse();
		}
		else throw Poco::OpenFileException("cannot read from preprocessor");
	}

	int parseAll()
	{
		int errors = 0;
		std::set<std::string> files;
		buildFileList(files);
		for (std::set<std::string>::const_iterator it = files.begin(); it != files.end(); ++it)
		{
			try
			{
				parse(*it);
			}
			catch (Exception& exc)
			{
				logger().log(exc);
				++errors;
			}
		}
		return errors;
	}

	void fixup()
	{
		logger().information("Fixing-up class hierarchies");
		for (Poco::CppParser::NameSpace::SymbolTable::iterator it = _gst.begin(); it != _gst.end(); ++it)
		{
			Poco::CppParser::Struct* pStruct = dynamic_cast<Poco::CppParser::Struct*>(it->second);
			if (pStruct)
			{
				pStruct->fixupBases();
			}
		}
	}

	void writeDoc()
	{
		logger().information("Generating documentation");
		Path path(config().getString("PocoDoc.output", "doc"));
		path.makeDirectory();
		File file(path);
		file.createDirectories();

		if (_searchIndexEnabled || config().getBool("PocoDoc.searchIndex", false))
		{
#if defined(POCO_ENABLE_SQLITE_FTS5)
			std::string dbDirectory = path.toString() + DocWriter::DATABASE_DIR;
			Path dbPath(dbDirectory);
			dbPath.makeDirectory();
			File dbFile(dbPath);
			dbFile.createDirectories();
			_searchIndexEnabled = true;
#else
			logger().error("FTS5 is not enabled, search is not supported");
#endif
		}

		DocWriter writer(_gst, path.toString(), config().getBool("PocoDoc.prettifyCode", false), _writeEclipseTOC, _searchIndexEnabled);

		if (config().hasProperty("PocoDoc.pages"))
		{
			std::string pages = config().getString("PocoDoc.pages");
			StringTokenizer tokenizer(pages, ",\n", StringTokenizer::TOK_IGNORE_EMPTY | StringTokenizer::TOK_TRIM);
			std::set<std::string> pageSet;
			for (StringTokenizer::Iterator it = tokenizer.begin(); it != tokenizer.end(); ++it)
			{
				Glob::glob(*it, pageSet);
			}
			for (std::set<std::string>::const_iterator it = pageSet.begin(); it != pageSet.end(); ++it)
			{
				writer.addPage(*it);
			}
		}
		writer.write();

		if (_writeEclipseTOC)
		{
			writer.writeEclipseTOC();
		}
	}

	void copyResources()
	{
		logger().information("Copying resources");
		Path path(config().getString("PocoDoc.output", "doc"));

		if (config().hasProperty("PocoDoc.resources"))
		{
			std::string pages = config().getString("PocoDoc.resources");
			StringTokenizer tokenizer(pages, ",\n", StringTokenizer::TOK_IGNORE_EMPTY | StringTokenizer::TOK_TRIM);
			std::set<std::string> pageSet;
			for (StringTokenizer::Iterator it = tokenizer.begin(); it != tokenizer.end(); ++it)
			{
				Glob::glob(*it, pageSet);
			}
			for (std::set<std::string>::const_iterator it = pageSet.begin(); it != pageSet.end(); ++it)
			{
				try
				{
					copyResource(Path(*it), path);
				}
				catch (Poco::Exception& exc)
				{
					logger().log(exc);
				}
			}
		}
	}

	void copyResource(const Path& source, const Path& dest)
	{
		logger().information(std::string("Copying resource ") + source.toString() + " to " + dest.toString());
		File sf(source);
		if (sf.isDirectory())
			copyDirectory(source, dest);
		else
			copyFile(source, dest);
	}

	void copyFile(const Path& source, const Path& dest)
	{
		Path dd(dest);
		dd.makeDirectory();
		File df(dd);
		df.createDirectories();
		dd.setFileName(source.getFileName());
		if (source.getExtension() == "thtml")
		{
			dd.setExtension("html");
			std::ifstream istr(source.toString().c_str());
			std::ofstream ostr(dd.toString().c_str());
			while (istr.good())
			{
				std::string line;
				std::getline(istr, line);
				ostr << config().expand(line) << std::endl;
			}
		}
		else
		{
			File sf(source);
			sf.copyTo(dd.toString());
		}
	}

	void copyDirectory(const Path& source, const Path& dest)
	{
		Path src(source);
		src.makeFile();
		DirectoryIterator it(src);
		DirectoryIterator end;
		for (; it != end; ++it)
		{
			Path dd(dest);
			dd.makeDirectory();
			dd.pushDirectory(src.getFileName());
			copyResource(it.path(), dd);
		}
	}

	int main(const std::vector<std::string>& args)
	{
		if (!_helpRequested)
		{
			Poco::DateTime now;
			config().setString("PocoDoc.date", Poco::DateTimeFormatter::format(now, "%Y-%m-%d"));
			config().setString("PocoDoc.year", Poco::DateTimeFormatter::format(now, "%Y"));
			config().setString("PocoDoc.googleAnalyticsCode", generateGoogleAnalyticsCode());
			config().setString("PocoDoc.hubSpotCode", generateHubSpotCode());
			if (!config().has("PocoDoc.customHeadHTML")) config().setString("PocoDoc.customHeadHTML", "");
			if (!config().has("PocoDoc.customBodyHTML")) config().setString("PocoDoc.customBodyHTML", "");
			Poco::Stopwatch sw;
			int errors = 0;
			try
			{
				sw.start();
				errors = parseAll();
				fixup();
				writeDoc();
				copyResources();
				sw.stop();
			}
			catch (Exception& exc)
			{
				std::cerr << exc.displayText() << std::endl;
			}
			logger().information(NumberFormatter::format(errors) + " errors.");
			logger().information(std::string("Time: ") + Poco::DateTimeFormatter::format(Poco::Timespan(sw.elapsed())));
		}
		return Application::EXIT_OK;
	}

	std::string generateGoogleAnalyticsCode()
	{
		std::stringstream ostr;
		std::string googleAnalyticsId(config().getString("PocoDoc.googleAnalyticsId", ""));
		if (!googleAnalyticsId.empty())
		{
			ostr << "<script>\n";
			ostr << "  (function(i,s,o,g,r,a,m){i['GoogleAnalyticsObject']=r;i[r]=i[r]||function(){\n";
			ostr << "  (i[r].q=i[r].q||[]).push(arguments)},i[r].l=1*new Date();a=s.createElement(o),\n";
			ostr << "  m=s.getElementsByTagName(o)[0];a.async=1;a.src=g;m.parentNode.insertBefore(a,m)\n";
			ostr << "  })(window,document,'script','//www.google-analytics.com/analytics.js','ga');\n";
			ostr << "\n";
			ostr << "  ga('create', '" << googleAnalyticsId << "', 'auto');\n";
			ostr << "  ga('set', 'anonymizeIp', true);\n";
			ostr << "  ga('send', 'pageview');\n";
			ostr << "</script>\n";
		}
		return ostr.str();
	}

	std::string generateHubSpotCode()
	{
		std::stringstream ostr;
		std::string hubSpotId(config().getString("PocoDoc.hubSpotId", ""));
		if (!hubSpotId.empty())
		{
			ostr << "<script type=\"text/javascript\" id=\"hs-script-loader\" async defer src=\"//js.hs-scripts.com/" << hubSpotId << ".js\"></script>\n";
		}
		return ostr.str();
	}

private:
	bool _helpRequested;
	bool _writeEclipseTOC;
	bool _searchIndexEnabled;
	Poco::CppParser::NameSpace::SymbolTable _gst;
};


int main(int argc, char** argv)
{
	PocoDocApp app;
	try
	{
		app.init(argc, argv);
	}
	catch (Poco::Exception& exc)
	{
		app.logger().log(exc);
		return Application::EXIT_CONFIG;
	}
	return app.run();
}
