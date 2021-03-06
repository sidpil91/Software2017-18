#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
#include <cctype>
#include <future>
#include <thread>
#include <chrono>
#include <mutex>
#include <boost/optional.hpp>

#include "course.hpp"
#include "player.hpp"

using Message = std::pair<boost::optional<int>, std::vector<std::string>>;

static Message readInt(std::unique_ptr<boost::process::ipstream>& in) {
  std::vector<string> msg;
  if (!in) {
    msg.push_back("input stream is closed");
    return std::make_pair(boost::none, msg);
  }
  int num;
  std::string str;
  *in >> str;
  try {
    num = std::stoi(str);
  } catch (const std::invalid_argument& e) {
    std::string clipped;
    if (str.length() >= 100) {
      str = str.substr(0, 100) + "...";
      clipped = "(clipped)";
    }
    msg.push_back("input invalid argument from AI : \"" + str + "\"" + clipped);
    msg.push_back("what : " + std::string(e.what()));
    return std::make_pair(boost::none, msg);
  } catch (const std::out_of_range& e) {
    std::string clipped;
    if (str.length() >= 100) {
      str = str.substr(0, 100) + "...";
      clipped = "(clipped)";
    }
    msg.push_back("input out of int range value from AI : \"" + str + "\"" + clipped);
    msg.push_back("what : " + std::string(e.what()));
    return std::make_pair(boost::none, msg);
  }
  return std::make_pair(boost::optional<int>(num), msg);
}

static void handShake(std::unique_ptr<boost::process::ipstream> in, std::promise<std::pair<std::unique_ptr<boost::process::ipstream>, Message>> p)
{
  auto ans = readInt(in);
  p.set_value(make_pair(std::move(in), ans));
}

void sendToAI(std::unique_ptr<boost::process::opstream>&  toAI, std::shared_ptr<std::ofstream> stdinLogStream, const char *fmt, int value) {
  int n = std::snprintf(nullptr, 0, fmt, value);
  auto cstr = new std::unique_ptr<char[]>(new char[n + 2]);
  std::snprintf(cstr->get(), n + 1, fmt, value);
  std::string str(cstr->get());
  *toAI << str;
  if (stdinLogStream.get() != nullptr) {
    *stdinLogStream << str;
  }
}

void flushToAI(std::unique_ptr<boost::process::opstream>& toAI, std::shared_ptr<std::ofstream> stdinLogStream) {
  toAI->flush();
  if (stdinLogStream.get() != nullptr) stdinLogStream->flush();
}

static void logging(std::promise<void> promise, std::unique_ptr<std::istream> input, std::shared_ptr<std::ostream> output, int MAX_SIZE) {
  if (output) {
    int size = 0;
    for (; MAX_SIZE == -1 || size < MAX_SIZE; ++size) {
      char c;
      if (input->get(c)) {
        std::lock_guard<std::mutex> lock(mutex);
        *output << c;
      } else {
        break;
      }
    }
    if (MAX_SIZE != -1 && size >= MAX_SIZE) {
      *output << std::endl;
      *output << "[system] stderr output have reached the limit(MAX_SIZE=" << MAX_SIZE << " bytes)" << std::endl;
    }
  }
  promise.set_value();
}

Logger::Logger(std::unique_ptr<std::istream> input, std::shared_ptr<std::ostream> output, int MAX_SIZE = -1) {
  std::promise<void> promise;
  future = promise.get_future();
  thread = std::thread(logging, std::move(promise), std::move(input), output, MAX_SIZE);
}

Logger::~Logger() {
  mutex.unlock();
  future.wait_for(std::chrono::milliseconds(100));
  thread.join();
}

Player::Player(string command, const RaceCourse &course, int xpos,
         string name, const Option &opt):
  name(name), position(Point(xpos, 0)), velocity(0, 0),
  timeLeft(course.thinkTime), status(Status::VALID),
  stdinLogStream(opt.stdinLogStream), stderrLogStream(opt.stderrLogStream),
  pauseCommand(opt.pauseCommand), resumeCommand(opt.resumeCommand) {
  auto env = boost::this_process::environment();
  std::error_code error_code_child;
  std::unique_ptr<boost::process::ipstream> stderrFromAI(new boost::process::ipstream);
  toAI = std::unique_ptr<boost::process::opstream>(new boost::process::opstream);
  fromAI = std::unique_ptr<boost::process::ipstream>(new boost::process::ipstream);
  child = std::unique_ptr<boost::process::child>(new boost::process::child(
    command,
    boost::process::std_out > *fromAI,
    boost::process::std_err > *stderrFromAI,
    boost::process::std_in < *toAI,
    env,
    error_code_child
  ));
  if (stderrLogStream) {
    *stderrLogStream << "[system] Try : hand shake" << endl;
  }
  stderrLogger = std::unique_ptr<Logger>(new Logger(std::move(stderrFromAI), stderrLogStream, 1 << 15));
  sendToAI(toAI, stdinLogStream, "%d\n", course.thinkTime);
  sendToAI(toAI, stdinLogStream, "%d\n", course.stepLimit);
  sendToAI(toAI, stdinLogStream, "%d ", course.width);
  sendToAI(toAI, stdinLogStream, "%d\n", course.length);
  sendToAI(toAI, stdinLogStream, "%d\n", course.vision);
  flushToAI(toAI, stdinLogStream);
  std::promise<std::pair<std::unique_ptr<boost::process::ipstream>, Message>> promise;
  std::future<std::pair<std::unique_ptr<boost::process::ipstream>, Message>> future = promise.get_future();
  std::chrono::milliseconds remain(timeLeft);
  std::thread thread(handShake, std::move(fromAI), std::move(promise));
  std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
  std::future_status result = future.wait_for(remain);
  std::chrono::system_clock::time_point end = std::chrono::system_clock::now();
  auto timeUsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
  timeLeft -= timeUsed;
  thread.join();
  stderrLogger->mutex.lock();
  if (stderrLogStream) {
    *stderrLogStream << "[system] spend time: " << timeUsed << ", remain: " << timeLeft << endl;
  }
  if (pauseCommand) {
    std::error_code ec;
    int result = boost::process::system(pauseCommand.get(), ec);
    std::cerr << __FILE__ << ":" << __LINE__ << ": [pause] (" << name << ") return code : " << result << ", error value : " << ec.value() << ", error message : " << ec.message() << std::endl;
  }
  if (result == std::future_status::timeout) {
    status = Status::TIMEOUT;
    std::cerr << "player : \"" << name << "\" has been timeouted" << std::endl;
    if (stderrLogStream) {
      *stderrLogStream << "your AI : \"" << name << "\" has been timeouted" << std::endl;
    }
    return;
  }
  auto ret = future.get();
  fromAI = std::move(ret.first);
  auto ans = ret.second.first;
  for (const auto& line : ret.second.second) {
    std::cerr << line << std::endl;
    if (stderrLogStream) {
      *stderrLogStream << "[system] " << line << std::endl;
    }
  }
  if (!ans || ans.get() != 0) {
    if (stderrLogStream) {
      *stderrLogStream << "[system] Failed... : hand shake" << endl;
    }
    if (!child->running()) {
      std::cerr << "player : \"" << name << "\" is died." << std::endl;
      std::cerr << "\texit code : " << child->exit_code() << std::endl;
      if (stderrLogStream) {
        *stderrLogStream << "[system] your AI : \"" << name << "\" is died." << std::endl;
        *stderrLogStream << "[system] \texit code : " << child->exit_code() << std::endl;
      }
      status = Status::DIED;
      return;
    }
    if (ans) {
      int v = ans.get();
      cerr << "Response at initialization of player \"" << name << "\" : ("
     << v << ") is non-zero" << endl;
      if (stderrLogStream) {
        *stderrLogStream << "[system] Response at initialization of player \"" << name << "\" : (" << v << ") is non-zero" << std::endl;
      }
    }
    status = Status::INVALID;
  } else { 
    if (stderrLogStream) {
      *stderrLogStream << "[system] Success! : hand shake" << endl;
    }
  }
}

using Message4Act = std::pair<boost::optional<std::pair<int, int>>, std::vector<std::string>>;

static void readAct(std::unique_ptr<boost::process::ipstream> in, std::promise<std::pair<std::unique_ptr<boost::process::ipstream>, Message4Act>> p)
{
  auto ax = readInt(in);
  auto ay = readInt(in);

  std::vector<std::string> msg;
  msg.insert(msg.end(), ax.second.begin(), ax.second.end());
  msg.insert(msg.end(), ay.second.begin(), ay.second.end());
  if (ax.first && ay.first) {
    p.set_value(
      make_pair(
        std::move(in),
        make_pair(
          boost::optional<std::pair<int, int>>(make_pair(ax.first.get(), ay.first.get())),
          msg
        )
      )
    );
  } else {
    p.set_value(make_pair(std::move(in), make_pair(boost::none, msg)));
  }
}

IntVec Player::play(int c, Player &op, RaceCourse &course) {
  if (stderrLogStream) {
    *stderrLogStream << "[system] ================================" << std::endl;
    *stderrLogStream << "[system] turn: " << c << std::endl;
  }
  sendToAI(toAI, stdinLogStream, "%d\n", c);
  sendToAI(toAI, stdinLogStream, "%d\n", timeLeft);
  sendToAI(toAI, stdinLogStream, "%d ", position.x);
  sendToAI(toAI, stdinLogStream, "%d ", position.y);
  sendToAI(toAI, stdinLogStream, "%d ", velocity.x);
  sendToAI(toAI, stdinLogStream, "%d\n", velocity.y);
  sendToAI(toAI, stdinLogStream, "%d ", op.position.x);
  sendToAI(toAI, stdinLogStream, "%d ", op.position.y);
  sendToAI(toAI, stdinLogStream, "%d ", op.velocity.x);
  sendToAI(toAI, stdinLogStream, "%d\n", op.velocity.y);
  for (int y = position.y-course.vision;
       y <= position.y+course.vision;
       y++) {
    for (int x = 0; x != course.width; x++) {
      sendToAI(toAI, stdinLogStream, "%d ",
         (y < 0 || y > course.length ||
    course.obstacle[x][y] ? 1 : 0));
    }
    sendToAI(toAI, stdinLogStream, "\n", 0);
  }
  flushToAI(toAI, stdinLogStream);
  std::promise<std::pair<std::unique_ptr<boost::process::ipstream>, Message4Act>> promise;
  std::future<std::pair<std::unique_ptr<boost::process::ipstream>, Message4Act>> future = promise.get_future();
  stderrLogger->mutex.unlock();
  if (resumeCommand) {
    std::error_code ec;
    int result = boost::process::system(resumeCommand.get(), ec);
    std::cerr << __FILE__ << ":" << __LINE__ << ": [resume] (" << name << ") return code : " << result << ", error value : " << ec.value() << ", error message : " << ec.message() << std::endl;
  }
  std::chrono::milliseconds remain(timeLeft);
  std::thread thread(readAct, std::move(fromAI), std::move(promise));
  std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
  std::future_status result = future.wait_for(remain);
  std::chrono::system_clock::time_point end = std::chrono::system_clock::now();
  auto timeUsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
  timeLeft -= timeUsed;
  thread.join();
  stderrLogger->mutex.lock();
  if (stderrLogStream) {
    *stderrLogStream << "[system] spend time: " << timeUsed << ", remain: " << timeLeft << endl;
  }
  if (pauseCommand) {
    std::error_code ec;
    int result = boost::process::system(pauseCommand.get(), ec);
    std::cerr << __FILE__ << ":" << __LINE__ << ": [pause] (" << name << ") return code : " << result << ", error value : " << ec.value() << ", error message : " << ec.message() << std::endl;
  }
  if (result == std::future_status::timeout) {
    status = Status::TIMEOUT;
    std::cerr << "player : " << name << " has been timeouted" << std::endl;
    if (stderrLogStream) {
      *stderrLogStream << "[system] your AI : \"" << name << "\" has been timeouted" << std::endl;
    }
    return IntVec();
  }
  auto ret = future.get();
  fromAI = std::move(ret.first);
  for (const auto& line : ret.second.second) {
    std::cerr << line << std::endl;
    if (stderrLogStream) {
      *stderrLogStream << "[system] " << line << std::endl;
    }
  }
  if (ret.second.first) {
    auto val = ret.second.first.get();
    if (val.first < -1 || 1 < val.first
      || val.second < -1 || 1 < val.second) {
      std::cerr << "acceleration value must be from -1 to 1 each axis, but player : \"" << name << "\" saied : (" << val.first << ", " << val.second << ")" << std::endl;
      if (stderrLogStream) {
        *stderrLogStream << "[system] acceleration value must be from -1 to 1 each axis, but your AI : \"" << name << "\" saied : (" << val.first << ", " << val.second << ")" << std::endl;
      }
      status = Status::INVALID;
      return IntVec();
    }
    return IntVec(val.first, val.second);
  } else {
    if (!child->running()) {
      std::cerr << "player : \"" << name << "\" is died." << std::endl;
      std::cerr << "\texit code : " << child->exit_code() << std::endl;
      if (stderrLogStream) {
        *stderrLogStream << "[system] your AI : \"" << name << "\" is died." << std::endl;
        *stderrLogStream << "[system] \texit code : " << child->exit_code() << std::endl;
      }
      status = Status::DIED;
      return IntVec();
    }
    status = Status::INVALID;
    return IntVec();
  }
}

void Player::terminate() {
  child->terminate();
}

