
#include <cassert>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>

#include <geometrycentral/numerical/linear_solvers.h>
#include <geometrycentral/surface/halfedge_mesh.h>
#include <geometrycentral/surface/intrinsic_geometry_interface.h>
#include <geometrycentral/surface/vertex_position_geometry.h>
#include <geometrycentral/utilities/vector3.h>

#include <Eigen/Core>

#include <pcg_random.hpp>

#include "ddgsolver/force.h"

namespace ddgsolver {

namespace gc = ::geometrycentral;
namespace gcs = ::geometrycentral::surface;

void Force::getVelocityFromPastPosition(double dt) {
  auto vertexVelocity_e = ddgsolver::EigenMap<double, 3>(vertexVelocity);
  auto pos_e = ddgsolver::EigenMap<double, 3>(vpg.inputVertexPositions);
  auto pastpos_e = ddgsolver::EigenMap<double, 3>(pastPositions);
  vertexVelocity_e = (pos_e - pastpos_e) / dt;
}

void Force::getDPDForces() {
  auto dampingForces_e = ddgsolver::EigenMap<double, 3>(dampingForces);
  auto stochasticForces_e = ddgsolver::EigenMap<double, 3>(stochasticForces);
  dampingForces_e.setZero();
  stochasticForces_e.setZero();

  // std::default_random_engine random_generator;
  gcs::EdgeData<double> random_var(mesh);
  std::normal_distribution<double> normal_dist(0, sigma);

  for (gcs::Edge e : mesh.edges()) {
    random_var[e] = normal_dist(rng);
  }

  // alias positions
  const auto &pos = vpg.inputVertexPositions;

  for (gcs::Vertex v : mesh.vertices()) {
    for (gcs::Halfedge he : v.outgoingHalfedges()) {
      auto v_adj = he.next().vertex();

      gc::Vector3 dVel = vertexVelocity[v] - vertexVelocity[v_adj];
      gc::Vector3 dPos_n = (pos[v] - pos[v_adj]).normalize();
      dampingForces[v] += -gamma * (gc::dot(dVel, dPos_n) * dPos_n);
      stochasticForces[v] += random_var[he.edge()] * dPos_n;
    }
  }
}

void Force::pcg_test() {
  // Generate a normal distribution around that mean
  std::normal_distribution<> normal_dist(0, 2);

  // Make a copy of the RNG state to use later
  pcg32 rng_checkpoint = rng;

  // Produce histogram
  std::map<int, int> hist;
  for (int n = 0; n < 10000; ++n) {
    ++hist[std::round(normal_dist(rng))];
  }
  std::cout << "Normal distribution around " << 0 << ":\n";
  for (auto p : hist) {
    std::cout << std::fixed << std::setprecision(1) << std::setw(2) << p.first
              << ' ' << std::string(p.second / 30, '*') << '\n';
  }

  // Produce information about RNG usage
  std::cout << "Required " << (rng - rng_checkpoint) << " random numbers.\n";
}

} // end namespace ddgsolver