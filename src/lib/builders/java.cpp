#include "java.hpp"

#include <bunsan/static_initializer.hpp>

#include <boost/assert.hpp>
#include <boost/regex.hpp>
#include <boost/scope_exit.hpp>

namespace bacs {
namespace system {
namespace builders {

BUNSAN_STATIC_INITIALIZER(bacs_system_builders_java, {
  BUNSAN_FACTORY_REGISTER_TOKEN(builder, java,
                                [](const std::vector<std::string> &arguments) {
                                  return builder::make_shared<java>(arguments);
                                })
})

static const boost::regex positional("[^=]+");
static const boost::regex key_value("([^=]+)=(.*)");
static const boost::regex filename_error(
    ".*class (\\S+) is public, should be declared "
    "in a file named \\1.java.*");

java::java(const std::vector<std::string> &arguments, bool parse_name) {
  if (parse_name) m_java.reset(new java(arguments, false));
  for (const std::string &arg : arguments) {
    boost::smatch match;
    if (boost::regex_match(arg, match, positional)) {
      BOOST_ASSERT(match.size() == 1);
      m_flags.push_back("-" + arg);
    } else {
      BOOST_THROW_EXCEPTION(invalid_argument_error()
                            << invalid_argument_error::argument(arg));
    }
  }
}

executable_ptr java::build(const ContainerPointer &container,
                           const unistd::access::Id &owner_id,
                           const bacs::process::Source &source,
                           const bacs::process::ResourceLimits &resource_limits,
                           bacs::process::BuildResult &result) {
  try {
    if (m_java) {
      const executable_ptr solution =
          m_java->build(container, owner_id, source, resource_limits, result);
      if (solution) {
        return solution;
      } else {
        boost::smatch match;
        if (boost::regex_match(result.output(), match, filename_error)) {
          BOOST_ASSERT(match.size() == 2);
          m_java->m_class = match[1];
          return m_java->build(container, owner_id, source, resource_limits,
                               result);
        } else {
          return solution;  // null
        }
      }
    } else {
      // nested object
      return compilable::build(container, owner_id, source, resource_limits,
                               result);
    }
  } catch (std::exception &) {
    BOOST_THROW_EXCEPTION(builder_build_error()
                          << bunsan::enable_nested_current());
  }
}

compilable::name_type java::name(const bacs::process::Source & /*source*/) {
  return {.source = m_class + ".java", .executable = m_class};
}

ProcessPointer java::create_process(const ProcessGroupPointer &process_group,
                                    const name_type &name) {
  const ProcessPointer process = process_group->createProcess("javac");
  process->setArguments(process->executable(), name.source);
  return process;
}

executable_ptr java::create_executable(const ContainerPointer &container,
                                       bunsan::tempfile &&tmpdir,
                                       const name_type &name) {
  return std::make_shared<java_executable>(container, std::move(tmpdir), name,
                                           "java", m_flags);
}

std::vector<std::string> java_executable::arguments() const {
  std::vector<std::string> arguments_ = flags();
  arguments_.push_back("-classpath");
  arguments_.push_back(dir().string());
  arguments_.push_back(name().executable.string());
  return arguments_;
}

}  // namespace builders
}  // namespace system
}  // namespace bacs
