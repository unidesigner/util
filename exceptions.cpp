#include <sys/prctl.h>
#include <sys/wait.h>

#include <boost/lexical_cast.hpp>

#include "Logger.h"
#include "demangle.h"
#include "exceptions.h"

logger::LogChannel tracelog("tracelog", "[trace] ");

stack_trace_::stack_trace_() {

	std::string programName = get_program_name();
	std::string pid         = get_pid();

	_stack_trace.push_back(std::string("[trace] back trace for ") + programName + " (" + pid + "):");

	// create a pipe to read gdb's output
	int pipefds[2];
	pipe(pipefds);

	// create a pipe to be used as a barrier
	int barrierfds[2];
	pipe(barrierfds);

	// fork
	int childPid = fork();

	// child:
	if (!childPid) {

		LOG_ALL(tracelog) << "[child] waiting for parent process to allow attaching" << std::endl;

		// close writing end of barrier pipe
		close(barrierfds[1]);

		// wait until parent closes barrier pipe
		char buf[1];
		while (read(barrierfds[0], buf, sizeof(buf)) > 0)
			LOG_ALL(tracelog) << "[child] " << buf[0] << std::endl;

		LOG_ALL(tracelog) << "[child] parent process closed barrier pipe, preparing gdb invocation" << std::endl;

		// close barrier pipe
		close(barrierfds[0]);

		// close reading end of output pipe
		close(pipefds[0]);

		// redirect stdout and stderr to output pipe
		dup2(pipefds[1], 1);
		dup2(pipefds[1], 2);

		// close writing end of pipe (_we_ don't need it any longer)
		close(pipefds[1]);

		// start gdb
		execlp("gdb", "gdb", "--batch", "-n", "-ex", "bt full", programName.c_str(), pid.c_str(), NULL);

	// parent:
	} else {

		LOG_ALL(tracelog) << "[parent] allowing child to attach" << std::endl;

		// allow our child process to attach
		prctl(PR_SET_PTRACER, childPid, 0, 0, 0);

		LOG_ALL(tracelog) << "[parent] closing barrier pipe" << std::endl;

		// close barrier pipe to let child proceed
		close(barrierfds[0]);
		close(barrierfds[1]);

		LOG_ALL(tracelog) << "[parent] barrier pipe closed" << std::endl;

		// close the write end of pipe
		close(pipefds[1]);

		// capture child's output
		std::string output;

		// read the whole output of gdb
		char   buf[1];
		size_t n;
		while (n = read(pipefds[0], buf, sizeof(buf)))
			output += std::string(buf, n);

		LOG_ALL(tracelog) << "[parent] end of pipe; I read: " << std::endl << output << std::endl;

		// split it at newline characters
		std::stringstream oss(output);
		std::string       line;

		// ignore every line until '#0 ...'
		while (std::getline(oss, line) && (line.size() < 2 || line[0] != '#' && line[1] != '0'));

		// copy remaining lines to stack trace
		do {

			if (line.size() > 0 && line[0] != '\n')
				_stack_trace.push_back(std::string("[trace] ") + line);

		} while (std::getline(oss, line));

		// wait for the child to finish
		waitpid(childPid, NULL, 0);
	}

	return;
}

const std::vector<std::string>&
stack_trace_::get_stack_trace() const {

	return _stack_trace;
}

const std::string&
stack_trace_::get_program_name() {

	if (_program_name == "")
		initialise_program_name();

	return _program_name;
}

std::string
stack_trace_::get_pid() {

	return boost::lexical_cast<std::string>(getpid());
}

void
stack_trace_::initialise_program_name() {

	char link[1024];
	char name[1024];

	snprintf(link, sizeof link, "/proc/%d/exe", getpid());

	int size = readlink(link, name, sizeof link);
	if (size == -1) {

		_program_name = "[program name not found]";
		return;
	}

	for (int i = 0; i < size; i++)
		_program_name += name[i];
}
