function m = size(t,idx)
%SIZE Sparse tensor dimensions.
%  
%   D = SIZE(T) returns the size of the tensor.  
%
%   I = size(T,DIM) returns the sizes of the dimensions specified by DIM,
%   which is either a scalar or a vector of dimensions.
%
%   See also SPTENSOR_GT, SPTENSOR_GT/NDIMS.
%
%MATLAB Tensor Toolbox.
%Copyright 2015, Sandia Corporation.

% This is the MATLAB Tensor Toolbox by T. Kolda, B. Bader, and others.
% http://www.sandia.gov/~tgkolda/TensorToolbox.
% Copyright (2015) Sandia Corporation. Under the terms of Contract
% DE-AC04-94AL85000, there is a non-exclusive license for use of this
% work by or on behalf of the U.S. Government. Export of this data may
% require a license from the United States Government.
% The full license terms can be found in the file LICENSE.txt


if exist('idx','var')
    m = double(t.size(idx));
else
    m = double(t.size);
end
