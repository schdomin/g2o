// g2o - General Graph Optimization
// Copyright (C) 2011 R. Kuemmerle, G. Grisetti, W. Burgard
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
// TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
// PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
// TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "edge_pointxyz.h"

#ifdef G2O_HAVE_OPENGL
#include "g2o/stuff/opengl_wrapper.h"
#include "g2o/stuff/opengl_primitives.h"
#endif

namespace g2o {

  EdgePointXYZ::EdgePointXYZ() :
    BaseBinaryEdge<3, Vector3D, VertexPointXYZ, VertexPointXYZ>()
  {
    _information.setIdentity();
    _error.setZero();
  }

  bool EdgePointXYZ::read(std::istream& is)
  {
    Vector3D p;
    is >> p[0] >> p[1] >> p[2];
    setMeasurement(p);
    for (int i = 0; i < 3; ++i)
      for (int j = i; j < 3; ++j) {
        is >> information()(i, j);
        if (i != j)
          information()(j, i) = information()(i, j);
      }
    return true;
  }

  bool EdgePointXYZ::write(std::ostream& os) const
  {
    Vector3D p = measurement();
    os << p.x() << " " << p.y() << " " << p.z();
    for (int i = 0; i < 3; ++i)
      for (int j = i; j < 3; ++j)
        os << " " << information()(i, j);
    return os.good();
  }


#ifndef NUMERIC_JACOBIAN_THREE_D_TYPES
  void EdgePointXYZ::linearizeOplus()
  {
    _jacobianOplusXi=-Matrix3D::Identity();
    _jacobianOplusXj= Matrix3D::Identity();
  }
#endif

#ifdef G2O_HAVE_OPENGL

    EdgePointXYZDrawAction::EdgePointXYZDrawAction( ): DrawAction( typeid( EdgePointXYZ ).name( ) ){ }

    HyperGraphElementAction* EdgePointXYZDrawAction::operator( )( HyperGraph::HyperGraphElement* element, HyperGraphElementAction::Parameters*  params_ )
    {
        if (typeid(*element).name()!=_typeName)
          return 0;
        refreshPropertyPtrs(params_);
        if (! _previousParams)
          return this;

        if (_show && !_show->value())
          return this;

        EdgePointXYZ* e =  static_cast<EdgePointXYZ*>(element);
        VertexPointXYZ* fromVertex = static_cast<VertexPointXYZ*>(e->vertices()[0]);
        VertexPointXYZ* toVertex   = static_cast<VertexPointXYZ*>(e->vertices()[1]);
        if (! fromVertex || ! toVertex)
          return this;
        glColor3f(POSE_EDGE_COLOR);
        glPushAttrib(GL_ENABLE_BIT);
        glDisable(GL_LIGHTING);
        glBegin(GL_LINES);
        glVertex3f((float)fromVertex->estimate().x(),(float)fromVertex->estimate().y(),(float)fromVertex->estimate().z());
        glVertex3f((float)toVertex->estimate().x(),(float)toVertex->estimate().y(),(float)toVertex->estimate().z());
        glEnd();
        glPopAttrib();
        return this;
    }

#endif

} // end namespace
