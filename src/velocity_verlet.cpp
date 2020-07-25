#include "ddgsolver/force.h"
#include "ddgsolver/integrator.h"
#include "ddgsolver/meshops.h"

#include <geometrycentral/surface/halfedge_mesh.h>
#include <geometrycentral/surface/vertex_position_geometry.h>
#include <geometrycentral/utilities/vector3.h>
#include <geometrycentral/surface/meshio.h>

#include <Eigen/Core>

#include <pcg_random.hpp>

#include <iostream>

namespace ddgsolver {
  namespace integration {
    namespace gc = ::geometrycentral;
    namespace gcs = ::geometrycentral::surface;

    void velocityVerlet(Force& f, double dt, double total_time,
      double tolerance, double closeZone, double increment, double tSave, std::string outputDir) {

      // print out a .txt file listing all parameters used 
      getParameterLog(f, dt, total_time, tolerance, tSave, outputDir);

      Eigen::Matrix<double, Eigen::Dynamic, 3> force;
      Eigen::Matrix<double, Eigen::Dynamic, 3> newForce;
      force.resize(f.mesh.nVertices(), 3);
      force.setZero();
      newForce.resize(f.mesh.nVertices(), 3);
      newForce.setZero();

      int nSave = int(tSave / dt);

      auto vel_e = EigenMap<double, 3>(f.vel);
      auto pos_e = EigenMap<double, 3>(f.vpg.inputVertexPositions);

      const double hdt = 0.5 * dt;
      const double hdt2 = hdt * dt;

      Eigen::Matrix<double, Eigen::Dynamic, 3> staticForce;
      Eigen::Matrix<double, Eigen::Dynamic, 3> dynamicForce;

      double oldBE = 0.0;
      double BE;
      double dBE;
      double dArea;
      double dVolume;

      for (int i = 0; i <= total_time / dt; i++) {
        // Update all forces
        //f.getBendingForces();
        //f.getStretchingForces();
        //f.getPressureForces();
        f.getConservativeForces();
        f.getDPDForces();
        f.getExternalForces();

        //std::cout << "bf: " << EigenMap<double, 3>(f.bendingForces).norm()
        //  << "sf: " << EigenMap<double, 3>(f.stretchingForces).norm()
        //  << "pf: " << EigenMap<double, 3>(f.pressureForces).norm()
        //  << "df: " << EigenMap<double, 3>(f.dampingForces).norm()
        //  << "xf: " << EigenMap<double, 3>(f.stochasticForces).norm() << std::endl;

        staticForce = EigenMap<double, 3>(f.bendingForces) +
          EigenMap<double, 3>(f.stretchingForces) +
          EigenMap<double, 3>(f.pressureForces) +
          EigenMap<double, 3>(f.externalForces);
        staticForce -= staticForce.colwise().sum() / f.mesh.nVertices();
        dynamicForce = EigenMap<double, 3>(f.dampingForces) +
          EigenMap<double, 3>(f.stochasticForces);
        newForce = staticForce + dynamicForce;
        double normalStaticForceMax = rowwiseDotProduct(staticForce,
          EigenMap<double, 3>(f.vpg.vertexNormals)).rowwise().norm().maxCoeff();
    
        // periodically save the geometric files, print some info, compare and adjust
        if ((i % nSave == 0) || (i == int(total_time / dt))) {
          
          // 1. save
          f.richData.addGeometry(f.vpg);

          gcs::VertexData<double> H(f.mesh);
          H.fromVector(f.M_inv * f.H);
          f.richData.addVertexProperty("mean_curvature", H);

          gcs::VertexData<double> H0(f.mesh);
          H0.fromVector(f.H0);
          f.richData.addVertexProperty("spon_curvature", H0);

          gcs::VertexData<double> f_ext(f.mesh);
          f_ext.fromVector(f.appliedForceMagnitude);
          f.richData.addVertexProperty("external_force", f_ext);

          gcs::VertexData<double> fn(f.mesh);
          fn.fromVector(rowwiseDotProduct(staticForce,
            EigenMap<double, 3>(f.vpg.vertexNormals)));
          f.richData.addVertexProperty("normal_force", fn);

          gcs::VertexData<double> ft(f.mesh);
          ft.fromVector((staticForce - rowwiseScaling(rowwiseDotProduct(staticForce,
            EigenMap<double, 3>(f.vpg.vertexNormals)),
            EigenMap<double, 3>(f.vpg.vertexNormals))).rowwise().norm());
          f.richData.addVertexProperty("tangential_force", ft);

          gcs::VertexData<double> fb(f.mesh);
          fb.fromVector(rowwiseDotProduct(EigenMap<double, 3>(f.bendingForces),
            EigenMap<double, 3>(f.vpg.vertexNormals)));
          f.richData.addVertexProperty("bending_force", fb);

          /* gcs::VertexData<gc::Vector3> fn(f.mesh);
           EigenMap<double, 3>(fn) = rowwiseScaling((rowwiseDotProduct(staticForce,
             EigenMap<double, 3>(f.vpg.vertexNormals))), EigenMap<double, 3>(f.vpg.vertexNormals));
           f.richData.addVertexProperty("normal_force", fn);

           gcs::VertexData<gc::Vector3> ft(f.mesh);
           EigenMap<double, 3>(ft) = staticForce - EigenMap<double, 3>(fn);
           f.richData.addVertexProperty("tangential_force", ft);*/

          BE = getBendingEnergy(f);

          dBE = abs(BE - oldBE) / BE;
          dArea = abs(f.surfaceArea / f.targetSurfaceArea - 1);
          dVolume = abs(f.volume / f.maxVolume / f.P.Vt - 1);

          char buffer[50];
          sprintf(buffer, "t=%d.ply", int(i * dt * 100));
          f.richData.write(outputDir + buffer);

          if (i == int(total_time / dt)) {
            std::cout << "\n"
              << "Fail to converge in given time and Exit" << std::endl;
          }

          // 2. print
          std::cout << "\n"
            << "Time: " << i * dt << "\n"
            << "dArea: " << dArea << "\n"
            << "dVolume:  " << dVolume << "\n"
            << "dBE: " << dBE << "\n"
            << "Bending energy: " << BE << "\n";
           

          // 3.1 compare and adjust
          if (( dVolume < closeZone * tolerance)
            && ( dArea < closeZone * tolerance)
            && (dBE < closeZone * tolerance)) {
            double ref = std::max({ dVolume, dArea, dBE });
            f.P.kt *= 1 - dBE / ref * increment;
            f.P.Kv *= 1 + dVolume / ref * increment;
            f.P.Ksg *= 1 + dArea / ref * increment;

            std::cout << "Within the close zone below " << closeZone << " times tolerance("
                      << tolerance << "): " << "\n"
                      << "Increase global area penalty Ksg to " << f.P.Ksg << "\n"
                      << "Increase volume penalty Kv to " << f.P.Kv << "\n"
                      << "Decrese randomness kT to " << f.P.kt << "\n";

          }

          // 3.2 compare and exit
          if (( dVolume < tolerance)
            && ( dArea < tolerance)
            && ( dBE < tolerance)) {
            std::cout << "\n"
                      << "Converged! Saved to " + outputDir << std::endl;
            f.richData.write(outputDir + "final.ply");
            getSummaryLog(f, dt, i * dt, dArea, dVolume, dBE, BE, outputDir);
            break;
          }

          oldBE = getBendingEnergy(f);
        }

        pos_e += vel_e * dt + force * hdt2;

        vel_e += (force + newForce) * hdt;
        force = newForce;
        f.update_Vertex_positions(); // recompute cached values;

      }// periodic save, print and adjust

    }

  }// namespace integration
} // namespace ddgsolver
