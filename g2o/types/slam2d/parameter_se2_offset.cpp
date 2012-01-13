#include "parameter_se2_offset.h"

#include "vertex_se2.h"

#ifdef G2O_HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#endif

namespace g2o {

  ParameterSE2Offset::ParameterSE2Offset(){
    setOffset();
  }

  void ParameterSE2Offset::setOffset(const SE2& offset_){
    _offset = offset_;
    _offsetMatrix= _offset.rotation().toRotationMatrix();
    _offsetMatrix.translation() = _offset.translation();
    _inverseOffsetMatrix = _offsetMatrix.inverse();
  }

  bool ParameterSE2Offset::read(std::istream& is) {
    Vector3d off;
    for (int i=0; i<3; i++) {
      is >> off[i];
      std::cerr << off[i] << " " ;
    }
    std::cerr <<  std::endl;
    setOffset(SE2(off));
    return is.good();
  }
  
  bool ParameterSE2Offset::write(std::ostream& os) const {
    Vector3d off = _offset.toVector();
    for (int i=0; i<3; i++)
      os << off[i] << " ";
    return os.good();
  }

  CacheSE2Offset::CacheSE2Offset() :
    Cache(),
    _offsetParam(0)
  {
  }

  bool CacheSE2Offset::resolveDependancies(){
    _offsetParam = dynamic_cast <ParameterSE2Offset*> (_parameters[0]);
    return _offsetParam != 0;
  }

  void CacheSE2Offset::updateImpl(){
    const VertexSE2* v = static_cast<const VertexSE2*>(vertex());
    _se2_n2w = v->estimate() * _offsetParam->offset();

    _n2w = _se2_n2w.rotation().toRotationMatrix();
    _n2w.translation() = _se2_n2w.translation();

    _se2_w2n = _se2_n2w.inverse();
    _w2n = _se2_w2n.rotation().toRotationMatrix();
    _w2n.translation() = _se2_w2n.translation();

    SE2 w2l = v->estimate().inverse();
    _w2l = w2l.rotation().toRotationMatrix();
    _w2l.translation() = w2l.translation();

    double alpha=v->estimate().rotation().angle();
    double c=cos(alpha), s=sin(alpha);
    Matrix2d RInversePrime;
    RInversePrime << -s, c, -c, -s;
    _RpInverse_RInversePrime = _offsetParam->offset().rotation().toRotationMatrix().transpose()*RInversePrime;
    _RpInverse_RInverse=w2l.rotation();
  }  

  void CacheSE2Offset::setOffsetParam(ParameterSE2Offset* offsetParam)
  {
    _offsetParam = offsetParam;
  }


} // end namespace