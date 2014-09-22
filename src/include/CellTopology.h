//
//  CellTopology.h
//  Camellia
//
//  Created by Nate Roberts on 9/15/14.
//
//

#ifndef __Camellia__CellTopology__
#define __Camellia__CellTopology__

#include "Teuchos_RCP.hpp"
#include "Shards_CellTopology.hpp"

#include <map.h>
#include <pair.h>
#include <vector.h>

namespace Camellia {
  class CellTopology {
    typedef Teuchos::RCP<CellTopology> CellTopoPtr;
    
//    static map< unsigned, CellTopoPtr > _trilinosTopologies; // trilinos key --> our CellTopoPtr
    static map< pair<unsigned, unsigned>, CellTopoPtr > _tensorizedTrilinosTopologies; // (trilinos key, n) --> our CellTopoPtr for that cellTopo's nth-order tensor product with a line topology.  I.e. (shard::CellTopology::Line<2>::key, 2) --> a tensor-product hexahedron.  (This differs from the Trilinos hexahedron, because the enumeration of the sides of the quad in Shards goes counter-clockwise.)
    
    // members:
    shards::CellTopology _shardsBaseTopology;
    unsigned _tensorialDegree; // number of times we've tensor-producted the base topology with the line topology
    
    vector< vector<CellTopoPtr> > _subcells; // ordered by dimension, then ordinal
    
    CellTopology(const shards::CellTopology &baseTopo, unsigned tensorialDegree);
    
    // hypercube methods:
    static unsigned convertHypercubeOrdinalToShardsNodeOrdinal(unsigned spaceDim, unsigned hypercubeOrdinal);
    static unsigned convertShardsNodeOrdinalToHypercubeOrdinal(unsigned spaceDim, unsigned node_ord);
    static unsigned getHypercubeNode(const vector<unsigned> &address);
    static vector<unsigned> getHypercubeNodeAddress(unsigned spaceDim, unsigned node_ord); // 0s and 1s in the vector
    
    static pair<unsigned, vector<unsigned> > getHypercubePermutation(unsigned spaceDim, unsigned permutation_ordinal); // axis ordering number, followed by flips in each dimension
    vector<unsigned> getAxisChoices(unsigned axisChoiceOrdinal) const;
    unsigned getAxisChoiceOrdinal(const vector<unsigned> &axisChoices) const;
    
    vector< vector<unsigned> > _axisPermutations;
    map< vector<unsigned>, unsigned > _axisPermutationToOrdinal; // reverse lookup
    
    void computeAxisPermutations();
    
    static unsigned getNodeCount( const shards::CellTopology & shardsTopo ); // returns 1 for Node topology, unlike shards itself (which returns 0).
  public:
    /** \brief  The underlying shards topology */
    const shards::CellTopology & getShardsTopology() const;
    /** \brief  The number of times we have taken a tensor product between a line topology and the shards topology to form this cell topology */
    unsigned getTensorialDegree() const;
    
    /** \brief  Dimension of this cell topology */
    unsigned getDimension() const;
    
    /** \brief  Node count of this cell topology */
    unsigned getNodeCount() const;
    
    /** \brief  Vertex count of this cell topology */
    unsigned getVertexCount() const;
    
    /** \brief  Edge boundary subcell count of this cell topology */
    unsigned getEdgeCount() const;
    
    /** \brief  Face boundary subcell count of this cell topology */
    unsigned getFaceCount() const;
    
    /** \brief  Side boundary subcell count of this cell topology */
    unsigned getSideCount() const;
    
    /** \brief  Node count of a subcell of the given dimension and ordinal.
     *  \param  subcell_dim    [in]  - spatial dimension of the subcell
     *  \param  subcell_ord    [in]  - subcell ordinal
     */
    unsigned getNodeCount( const unsigned subcell_dim ,
                          const unsigned subcell_ord ) const;
    
    /** \brief  Vertex count of a subcell of the given dimension and ordinal.
     *  \param  subcell_dim    [in]  - spatial dimension of the subcell
     *  \param  subcell_ord    [in]  - subcell ordinal
     */
    unsigned getVertexCount( const unsigned subcell_dim ,
                            const unsigned subcell_ord ) const;
    
    
    /** \brief  Edge count of a subcell of the given dimension and ordinal.
     *  \param  subcell_dim    [in]  - spatial dimension of the subcell
     *  \param  subcell_ord    [in]  - subcell ordinal
     */
    unsigned getEdgeCount( const unsigned subcell_dim ,
                          const unsigned subcell_ord ) const;
    
    /** \brief  Side count of a subcell of the given dimension and ordinal.
     *  \param  subcell_dim    [in]  - spatial dimension of the subcell
     *  \param  subcell_ord    [in]  - subcell ordinal
     */
    unsigned getSideCount( const unsigned subcell_dim ,
                          const unsigned subcell_ord ) const;
    
    
    /** \brief  Subcell count of subcells of the given dimension.
     *  \param  subcell_dim    [in]  - spatial dimension of the subcell
     */
    unsigned getSubcellCount( const unsigned subcell_dim ) const;
    
    /** \brief  Mapping from a subcell's node ordinal to a
     *          node ordinal of this parent cell topology.
     *  \param  subcell_dim      [in]  - spatial dimension of the subcell
     *  \param  subcell_ord      [in]  - subcell ordinal
     *  \param  subcell_node_ord [in]  - node ordinal relative to subcell
     */
    unsigned getNodeMap( const unsigned  subcell_dim ,
                        const unsigned  subcell_ord ,
                        const unsigned  subcell_node_ord ) const;
    
    /** \brief  Number of node permutations defined for this cell */
    unsigned getNodePermutationCount() const;
    
    /** \brief  Permutation of a cell's node ordinals.
     *  \param  permutation_ordinal [in]
     *  \param  node_ordinal        [in]
     */
    unsigned getNodePermutation( const unsigned permutation_ord ,
                                const unsigned node_ord ) const;
    
    /** \brief  Inverse permutation of a cell's node ordinals.
     *  \param  permutation_ordinal [in]
     *  \param  node_ordinal        [in]
     */
    unsigned getNodePermutationInverse( const unsigned permutation_ord ,
                                       const unsigned node_ord ) const;
    
    CellTopoPtr getSubcell( unsigned scdim, unsigned scord ) const;
    
    bool isHypercube() const;
    
    /*** STATIC CONSTRUCTORS ***/
    // constructor from Trilinos CellTopology:
    static CellTopoPtr cellTopology(const shards::CellTopology &shardsCellTopo);
    static CellTopoPtr cellTopology(const shards::CellTopology &shardsCellTopo, unsigned tensorialDegree);
    
    // constructor for tensor of existing topology with line topology
    static CellTopoPtr lineTensorTopology(CellTopoPtr camelliaCellTopo);
    
    static CellTopoPtr point();
    static CellTopoPtr line();
    static CellTopoPtr quad();
    static CellTopoPtr hexahedron();
    
    static CellTopoPtr triangle();
    static CellTopoPtr tetrahedron();
//    static CellTopoPtr pyramid();
  };
}

typedef Teuchos::RCP<Camellia::CellTopology> CellTopoPtr;

#endif /* defined(__Camellia__CellTopology__) */
