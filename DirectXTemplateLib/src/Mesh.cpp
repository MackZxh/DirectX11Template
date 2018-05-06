#include <DirectXTemplateLibPCH.h>
#include <Mesh.h>

using namespace DirectX;
using namespace Microsoft::WRL;

const D3D11_INPUT_ELEMENT_DESC VertexPositionNormalTexture::InputElements[] =
{
    { "POSITION",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "NORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "TEXCOORD",   0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};

Mesh::Mesh()
    : m_IndexCount( 0 )
{}

Mesh::~Mesh()
{
    // Allocated resources will be cleaned automatically when the pointers go out of scope.
}

void Mesh::Draw( ID3D11DeviceContext* pDeviceContext )
{
    assert( pDeviceContext );

    const UINT strides[] = { sizeof(VertexPositionNormalTexture) };
    const UINT offsets[] = { 0 };

    pDeviceContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    pDeviceContext->IASetVertexBuffers( 0, 1, m_VertexBuffer.GetAddressOf(), strides, offsets );
    pDeviceContext->IASetIndexBuffer( m_IndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0 );
    pDeviceContext->DrawIndexed( m_IndexCount, 0, 0 );
}

std::unique_ptr<Mesh> Mesh::CreateSphere( ID3D11DeviceContext* pDeviceContext, float diameter, size_t tessellation, bool rhcoords )
{
    VertexCollection vertices;
    IndexCollection indices;

    if (tessellation < 3)
        throw std::out_of_range("tessellation parameter out of range");

    float radius = diameter / 2.0f;
    size_t verticalSegments = tessellation;
    size_t horizontalSegments = tessellation * 2;

    // Create rings of vertices at progressively higher latitudes.
    for (size_t i = 0; i <= verticalSegments; i++)
    {
        float v = 1 - (float)i / verticalSegments;

        float latitude = (i * XM_PI / verticalSegments) - XM_PIDIV2;
        float dy, dxz;

        XMScalarSinCos(&dy, &dxz, latitude);

        // Create a single ring of vertices at this latitude.
        for (size_t j = 0; j <= horizontalSegments; j++)
        {
            float u = (float)j / horizontalSegments;

            float longitude = j * XM_2PI / horizontalSegments;
            float dx, dz;

            XMScalarSinCos(&dx, &dz, longitude);

            dx *= dxz;
            dz *= dxz;

            XMVECTOR normal = XMVectorSet(dx, dy, dz, 0);
            XMVECTOR textureCoordinate = XMVectorSet(u, v, 0, 0);

            vertices.push_back(VertexPositionNormalTexture(normal * radius, normal, textureCoordinate));
        }
    }

    // Fill the index buffer with triangles joining each pair of latitude rings.
    size_t stride = horizontalSegments + 1;

    for (size_t i = 0; i < verticalSegments; i++)
    {
        for (size_t j = 0; j <= horizontalSegments; j++)
        {
            size_t nextI = i + 1;
            size_t nextJ = (j + 1) % stride;

            indices.push_back(uint16_t(i * stride + j));
            indices.push_back(uint16_t(nextI * stride + j));
            indices.push_back(uint16_t(i * stride + nextJ));

            indices.push_back(uint16_t(i * stride + nextJ));
            indices.push_back(uint16_t(nextI * stride + j));
            indices.push_back(uint16_t(nextI * stride + nextJ));
        }
    }

    // Create the primitive object.
    std::unique_ptr<Mesh> mesh(new Mesh());

    mesh->Initialize( pDeviceContext, vertices, indices, rhcoords );

    return mesh;
}

std::unique_ptr<Mesh> Mesh::CreateCube( ID3D11DeviceContext* deviceContext, float size, bool rhcoords )
{
    // A cube has six faces, each one pointing in a different direction.
    const int FaceCount = 6;

    static const XMVECTORF32 faceNormals[FaceCount] =
    {
        {  0,  0,  1 },
        {  0,  0, -1 },
        {  1,  0,  0 },
        { -1,  0,  0 },
        {  0,  1,  0 },
        {  0, -1,  0 },
    };

    static const XMVECTORF32 textureCoordinates[4] =
    {
        { 1, 0 },
        { 1, 1 },
        { 0, 1 },
        { 0, 0 },
    };

    VertexCollection vertices;
    IndexCollection indices;

    size /= 2;

    // Create each face in turn.
    for (int i = 0; i < FaceCount; i++)
    {
        XMVECTOR normal = faceNormals[i];

        // Get two vectors perpendicular both to the face normal and to each other.
        XMVECTOR basis = (i >= 4) ? g_XMIdentityR2 : g_XMIdentityR1;

        XMVECTOR side1 = XMVector3Cross(normal, basis);
        XMVECTOR side2 = XMVector3Cross(normal, side1);

        // Six indices (two triangles) per face.
        uint16_t vbase = uint16_t(vertices.size());
        indices.push_back(vbase + 0);
        indices.push_back(vbase + 1);
        indices.push_back(vbase + 2);

        indices.push_back(vbase + 0);
        indices.push_back(vbase + 2);
        indices.push_back(vbase + 3);

        // Four vertices per face.
        vertices.push_back(VertexPositionNormalTexture((normal - side1 - side2) * size, normal, textureCoordinates[0]));
        vertices.push_back(VertexPositionNormalTexture((normal - side1 + side2) * size, normal, textureCoordinates[1]));
        vertices.push_back(VertexPositionNormalTexture((normal + side1 + side2) * size, normal, textureCoordinates[2]));
        vertices.push_back(VertexPositionNormalTexture((normal + side1 - side2) * size, normal, textureCoordinates[3]));
    }

    // Create the primitive object.
    std::unique_ptr<Mesh> mesh(new Mesh());

    mesh->Initialize(deviceContext, vertices, indices, rhcoords);

    return mesh;
}

// Helper computes a point on a unit circle, aligned to the x/z plane and centered on the origin.
static inline XMVECTOR GetCircleVector(size_t i, size_t tessellation)
{
    float angle = i * XM_2PI / tessellation;
    float dx, dz;

    XMScalarSinCos(&dx, &dz, angle);

    XMVECTORF32 v = { dx, 0, dz, 0 };
    return v;
}

static inline XMVECTOR GetCircleTangent(size_t i, size_t tessellation)
{
    float angle = ( i * XM_2PI / tessellation ) + XM_PIDIV2;
    float dx, dz;

    XMScalarSinCos(&dx, &dz, angle);

    XMVECTORF32 v = { dx, 0, dz, 0 };
    return v;
}

// Helper creates a triangle fan to close the end of a cylinder / cone
static void CreateCylinderCap(VertexCollection& vertices, IndexCollection& indices, size_t tessellation, float height, float radius, bool isTop)
{
    // Create cap indices.
    for (size_t i = 0; i < tessellation - 2; i++)
    {
        size_t i1 = (i + 1) % tessellation;
        size_t i2 = (i + 2) % tessellation;

        if (isTop)
        {
            std::swap(i1, i2);
        }

        size_t vbase = vertices.size();
        indices.push_back(uint16_t(vbase));
        indices.push_back(uint16_t(vbase + i1));
        indices.push_back(uint16_t(vbase + i2));
    }

    // Which end of the cylinder is this?
    XMVECTOR normal = g_XMIdentityR1;
    XMVECTOR textureScale = g_XMNegativeOneHalf;

    if (!isTop)
    {
        normal = -normal;
        textureScale *= g_XMNegateX;
    }

    // Create cap vertices.
    for (size_t i = 0; i < tessellation; i++)
    {
        XMVECTOR circleVector = GetCircleVector(i, tessellation);

        XMVECTOR position = (circleVector * radius) + (normal * height);

        XMVECTOR textureCoordinate = XMVectorMultiplyAdd(XMVectorSwizzle<0, 2, 3, 3>(circleVector), textureScale, g_XMOneHalf);

        vertices.push_back(VertexPositionNormalTexture(position, normal, textureCoordinate));
    }
}

std::unique_ptr<Mesh> Mesh::CreateCone( ID3D11DeviceContext* deviceContext, float diameter, float height, size_t tessellation, bool rhcoords )
{
    VertexCollection vertices;
    IndexCollection indices;

    if (tessellation < 3)
        throw std::out_of_range("tessellation parameter out of range");

    height /= 2;

    XMVECTOR topOffset = g_XMIdentityR1 * height;

    float radius = diameter / 2;
    size_t stride = tessellation + 1;

    // Create a ring of triangles around the outside of the cone.
    for (size_t i = 0; i <= tessellation; i++)
    {
        XMVECTOR circlevec = GetCircleVector(i, tessellation);

        XMVECTOR sideOffset = circlevec * radius;

        float u = (float)i / tessellation;

        XMVECTOR textureCoordinate = XMLoadFloat(&u);

        XMVECTOR pt = sideOffset - topOffset;

        XMVECTOR normal = XMVector3Cross( GetCircleTangent( i, tessellation ), topOffset - pt );
        normal = XMVector3Normalize( normal );

        // Duplicate the top vertex for distinct normals
        vertices.push_back(VertexPositionNormalTexture(topOffset, normal, g_XMZero));
        vertices.push_back(VertexPositionNormalTexture(pt, normal, textureCoordinate + g_XMIdentityR1 ));

        indices.push_back(uint16_t(i * 2));
        indices.push_back(uint16_t((i * 2 + 3) % (stride * 2)));
        indices.push_back(uint16_t((i * 2 + 1) % (stride * 2)));
    }

    // Create flat triangle fan caps to seal the bottom.
    CreateCylinderCap(vertices, indices, tessellation, height, radius, false);

    // Create the primitive object.
    std::unique_ptr<Mesh> mesh(new Mesh());

    mesh->Initialize(deviceContext, vertices, indices, rhcoords);

    return mesh;
}

std::unique_ptr<Mesh> Mesh::CreateTorus(_In_ ID3D11DeviceContext* deviceContext, float diameter, float thickness, size_t tessellation, bool rhcoords)
{
    VertexCollection vertices;
    IndexCollection indices;

    if (tessellation < 3)
        throw std::out_of_range("tesselation parameter out of range");

    size_t stride = tessellation + 1;

    // First we loop around the main ring of the torus.
    for (size_t i = 0; i <= tessellation; i++)
    {
        float u = (float)i / tessellation;

        float outerAngle = i * XM_2PI / tessellation - XM_PIDIV2;

        // Create a transform matrix that will align geometry to
        // slice perpendicularly though the current ring position.
        XMMATRIX transform = XMMatrixTranslation(diameter / 2, 0, 0) * XMMatrixRotationY(outerAngle);

        // Now we loop along the other axis, around the side of the tube.
        for (size_t j = 0; j <= tessellation; j++)
        {
            float v = 1 - (float)j / tessellation;

            float innerAngle = j * XM_2PI / tessellation + XM_PI;
            float dx, dy;

            XMScalarSinCos(&dy, &dx, innerAngle);

            // Create a vertex.
            XMVECTOR normal = XMVectorSet(dx, dy, 0, 0);
            XMVECTOR position = normal * thickness / 2;
            XMVECTOR textureCoordinate = XMVectorSet(u, v, 0, 0);

            position = XMVector3Transform(position, transform);
            normal = XMVector3TransformNormal(normal, transform);

            vertices.push_back(VertexPositionNormalTexture(position, normal, textureCoordinate));

            // And create indices for two triangles.
            size_t nextI = (i + 1) % stride;
            size_t nextJ = (j + 1) % stride;

            indices.push_back(uint16_t(i * stride + j));
            indices.push_back(uint16_t(i * stride + nextJ));
            indices.push_back(uint16_t(nextI * stride + j));

            indices.push_back(uint16_t(i * stride + nextJ));
            indices.push_back(uint16_t(nextI * stride + nextJ));
            indices.push_back(uint16_t(nextI * stride + j));
        }
    }

    // Create the primitive object.
    std::unique_ptr<Mesh> mesh(new Mesh());

    mesh->Initialize(deviceContext, vertices, indices, rhcoords);

    return mesh;
}

// Helper for creating a D3D vertex or index buffer.
template<typename T>
static void CreateBuffer(_In_ ID3D11Device* device, T const& data, D3D11_BIND_FLAG bindFlags, _Outptr_ ID3D11Buffer** pBuffer)
{
    assert( pBuffer != 0 );

    D3D11_BUFFER_DESC bufferDesc = { 0 };

    bufferDesc.ByteWidth = (UINT)data.size() * sizeof(T::value_type);
    bufferDesc.BindFlags = bindFlags;
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;

    D3D11_SUBRESOURCE_DATA dataDesc = { 0 };

    dataDesc.pSysMem = data.data();

    HRESULT hr = device->CreateBuffer(&bufferDesc, &dataDesc, pBuffer);
    if ( FAILED(hr) )
    {
        throw std::exception("Failed to create buffer.");
    }
}

// Helper for flipping winding of geometric primitives for LH vs. RH coords
static void ReverseWinding( IndexCollection& indices, VertexCollection& vertices )
{
    assert( (indices.size() % 3) == 0 );
    for( auto it = indices.begin(); it != indices.end(); it += 3 )
    {
        std::swap( *it, *(it+2) );
    }

    for( auto it = vertices.begin(); it != vertices.end(); ++it )
    {
        it->textureCoordinate.x = ( 1.f - it->textureCoordinate.x );
    }
}

void Mesh::Initialize( ID3D11DeviceContext* deviceContext, VertexCollection& vertices, IndexCollection& indices, bool rhcoords )
{
    if ( vertices.size() >= USHRT_MAX )
        throw std::exception("Too many vertices for 16-bit index buffer");

    if ( !rhcoords )
        ReverseWinding( indices, vertices );

    ComPtr<ID3D11Device> device;
    deviceContext->GetDevice(device.ReleaseAndGetAddressOf());

    CreateBuffer( device.Get(), vertices, D3D11_BIND_VERTEX_BUFFER, m_VertexBuffer.ReleaseAndGetAddressOf() );
    CreateBuffer( device.Get(), indices, D3D11_BIND_INDEX_BUFFER, m_IndexBuffer.ReleaseAndGetAddressOf() );

    m_IndexCount = static_cast<UINT>( indices.size() );
}

