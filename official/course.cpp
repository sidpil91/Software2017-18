#include <algorithm>
#include "course.hpp"

static int dot(int x1, int y1, int x2, int y2) {
  return x1 * x2 + y1 * y2;
}

static int cross(int x1, int y1, int x2, int y2) {
  return x1 * y2 - x2 * y1;
}

static int ccw(int x1, int y1, int x2, int y2, int x3, int y3) {
  return cross(x2 - x1, y2 - y1, x3 - x2, y3 - y2);
}

bool LineSegment::goesThru(const Point &p) const {
  int minx = min(p1.x, p2.x);
  if (p.x < minx) return false;
  int maxx = max(p1.x, p2.x);
  if (p.x > maxx) return false;
  int miny = min(p1.y, p2.y);
  if (p.y < miny) return false;
  int maxy = max(p1.y, p2.y);
  if (p.y > maxy) return false;
  return ccw(p1.x, p1.y, p2.x, p2.y, p.x, p.y) == 0 && dot(p1.x - p.x, p1.y - p.y, p2.x - p.x, p2.y - p.y) <= 0;
}

bool LineSegment::intersects(const LineSegment &l) const {
  int minx = min(p1.x, p2.x);
  int maxx = max(p1.x, p2.x);
  int minlx = min(l.p1.x, l.p2.x);
  int maxlx = max(l.p1.x, l.p2.x);
  if (maxx < minlx || maxlx < minx ) return false;
  int miny = min(p1.y, p2.y);
  int maxy = max(p1.y, p2.y);
  int minly = min(l.p1.y, l.p2.y);
  int maxly = max(l.p1.y, l.p2.y);
  if (maxy < minly || maxly < miny ) return false;
  int d1 = (p1.x-l.p1.x)*(l.p2.y-l.p1.y)-(p1.y-l.p1.y)*(l.p2.x-l.p1.x);
  int d2 = (p2.x-l.p1.x)*(l.p2.y-l.p1.y)-(p2.y-l.p1.y)*(l.p2.x-l.p1.x);
  if (d1*d2 > 0) return false; 
  int d3 = (l.p1.x-p1.x)*(p2.y-p1.y)-(l.p1.y-p1.y)*(p2.x-p1.x);
  int d4 = (l.p2.x-p1.x)*(p2.y-p1.y)-(l.p2.y-p1.y)*(p2.x-p1.x);
  if (d3*d4 > 0) return false; 
  return true;
}

bool RaceCourse::collides(const LineSegment &m) const {
  int
    x1 = m.p1.x, y1 = m.p1.y,
    x2 = m.p2.x, y2 = m.p2.y;
  if (obstacle[x2][y2]) return true;
  int xstep = x2 > x1 ? 1 : -1;
  if (y1 == y2) {
    for (int x = x1; x != x2; x += xstep) {
      if (obstacle[x][y1]) return true;
    }
    return false;
  }
  int ystep = y2 > y1 ? 1 : -1;
  if (x1 == x2) {
    for (int y = y1; y != y2; y += ystep) {
      if (obstacle[x1][y]) return true;
    }
    return false;
  }
  for (int x = x1; x != x2; x += xstep) {
    int nx = x + xstep;
    for (int y = y1; y != y2; y += ystep) {
      int ny = y + ystep;
      if ((obstacle[x][y] && obstacle[nx][ny] &&
	   LineSegment(Point(x, y), Point(nx, ny)).intersects(m)) ||
	  (obstacle[x][ny] && obstacle[nx][ny] &&
	   LineSegment(Point(x, ny), Point(nx, ny)).intersects(m)) ||
	  (obstacle[nx][y] && obstacle[nx][ny] &&
	   LineSegment(Point(nx, y), Point(nx, ny)).intersects(m)) ||
	  (obstacle[x][ny] && obstacle[nx][y] &&
	   LineSegment(Point(x, ny), Point(nx, y)).intersects(m))) {
	return true;
      }
    }
  }
  return false;
}

const string CourseDataFileType = "race course";

RaceCourse::RaceCourse(istream &in) {
  read_json(in, courseTree);
  if (!courseTree.get_child_optional("filetype") ||
      courseTree.get<string>("filetype") != CourseDataFileType) {
    cerr << "The input file does not contain race course data" << endl;
    exit(1);
  }
  width = courseTree.get<int>("width");
  length = courseTree.get<int>("length");
  vision = courseTree.get<int>("vision");
  thinkTime = courseTree.get<int>("thinkTime");
  stepLimit = courseTree.get<int>("stepLimit");
  startX[0] = courseTree.get<int>("x0");
  startX[1] = courseTree.get<int>("x1");
  int y = 0;
  obstacle = vector<vector<bool>>(width);
  for (int x = 0; x != width; x++) obstacle[x] = vector<bool>(length);
  for (auto &row: courseTree.get_child("obstacles")) {
    int x = 0;
    for (auto &obst: row.second)
      obstacle[x++][y] = (obst.second.get_value<int>() != 0);
    y++;
  }
}

void IntVec::writeJson(ostream &out) {
  out << "{ \"x\": " << x << ", \"y\": " << y << " }";
}

void RaceCourse::writeJson(ostream &out) {
  out << "{" << endl
      << "   \"filetype\": \"race course\"," << endl
      << "   \"width\": " << width  << ", \"length\": " << length << ',' << endl
      << "   \"x0\": " << startX[0] << ", \"x1\": " << startX[1] << ',' << endl;
  out << "  \"obstacles\": [";
  for (int y = 0; y != length; y++) {
    out << "     [";
    for (int x = 0; x != width; x++) {
      out << (obstacle[x][y] ? '1' : '0')
	  << (x == width-1 ? "" : ", ");
    }
    out << "]"
	<< (y == length-1 ? "" : ",")
	<< endl;
  }
  out << "]" << endl
      << "}" << endl;
}
