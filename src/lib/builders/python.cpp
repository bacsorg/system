#include "python.hpp"

#include <bunsan/static_initializer.hpp>

#include <boost/assert.hpp>
#include <boost/regex.hpp>

namespace bacs {
namespace system {
namespace builders {

BUNSAN_STATIC_INITIALIZER(bacs_system_builders_python, {
  BUNSAN_FACTORY_REGISTER_TOKEN(
      builder, python, [](const std::vector<std::string> &arguments) {
        return builder::make_shared<python>(arguments);
      })
})

static const boost::regex positional("[^=]+"), key_value("([^=]+)=(.*)");

python::python(const std::vector<std::string> &arguments) {
  for (const std::string &arg : arguments) {
    boost::smatch match;
    if (boost::regex_match(arg, match, key_value)) {
      BOOST_ASSERT(match.size() == 3);
      const std::string key = match[1].str(), value = match[2].str();
      if (key == "lang") {
        m_lang = value;
      } else {
        BOOST_THROW_EXCEPTION(invalid_argument_error()
                              << invalid_argument_error::argument(arg));
      }
    } else {
      BOOST_THROW_EXCEPTION(invalid_argument_error()
                            << invalid_argument_error::argument(arg));
    }
  }
}

ProcessPointer python::create_process(const ProcessGroupPointer &process_group,
                                      const name_type &name) {
  const ProcessPointer process =
      process_group->createProcess("python" + m_lang);
  process->setArguments(process->executable(), "-c", R"EOF(
import sys
import py_compile

if __name__=='__main__':
    src = sys.argv[1]
    try:
        py_compile.compile(src, doraise=True)
    except py_compile.PyCompileError as e:
        sys.stderr.write(e.msg)
        sys.exit(1)

        )EOF",
                        name.source);
  return process;
}

executable_ptr python::create_executable(const ContainerPointer &container,
                                         bunsan::tempfile &&tmpdir,
                                         const name_type &name) {
  BOOST_ASSERT(m_flags.empty());
  return std::make_shared<interpretable_executable>(
      container, std::move(tmpdir), name, "python" + m_lang);
}

}  // namespace builders
}  // namespace system
}  // namespace bacs
