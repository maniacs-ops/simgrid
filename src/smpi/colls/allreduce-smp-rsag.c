/* Copyright (c) 2013-2014. The SimGrid Team.
 * All rights reserved.                                                     */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#include "colls_private.h"

/*
This fucntion performs all-reduce operation as follow.
1) binomial_tree reduce inside each SMP node
2) reduce-scatter -inter between root of each SMP node
3) allgather - inter between root of each SMP node
4) binomial_tree bcast inside each SMP node
*/
int smpi_coll_tuned_allreduce_smp_rsag(void *send_buf, void *recv_buf,
                                       int count, MPI_Datatype dtype, MPI_Op op,
                                       MPI_Comm comm)
{
  int comm_size, rank;
  void *tmp_buf;
  int tag = COLL_TAG_ALLREDUCE;
  int mask, src, dst;
  MPI_Status status;
  if(smpi_comm_get_leaders_comm(comm)==MPI_COMM_NULL){
    smpi_comm_init_smp(comm);
  }
  int num_core=1;
  if (smpi_comm_is_uniform(comm)){
    num_core = smpi_comm_size(smpi_comm_get_intra_comm(comm));
  }
  /*
     #ifdef MPICH2_REDUCTION
     MPI_User_function * uop = MPIR_Op_table[op % 16 - 1];
     #else
     MPI_User_function *uop;
     struct MPIR_OP *op_ptr;
     op_ptr = MPIR_ToPointer(op);
     uop  = op_ptr->op;
     #endif
   */
  comm_size = smpi_comm_size(comm);
  rank = smpi_comm_rank(comm);
  MPI_Aint extent;
  extent = smpi_datatype_get_extent(dtype);
  tmp_buf = (void *) smpi_get_tmp_sendbuffer(count * extent);

  int intra_rank, inter_rank;
  intra_rank = rank % num_core;
  inter_rank = rank / num_core;

  //printf("node %d intra_rank = %d, inter_rank = %d\n", rank, intra_rank, inter_rank);

  int inter_comm_size = (comm_size + num_core - 1) / num_core;

  if (!rank) {
    //printf("intra com size = %d\n",num_core);
    //printf("inter com size = %d\n",inter_comm_size);
  }


  smpi_mpi_sendrecv(send_buf, count, dtype, rank, tag,
               recv_buf, count, dtype, rank, tag, comm, &status);


  // SMP_binomial_reduce
  mask = 1;
  while (mask < num_core) {
    if ((mask & intra_rank) == 0) {
      src = (inter_rank * num_core) + (intra_rank | mask);
      //      if (src < ((inter_rank + 1) * num_core)) {
      if (src < comm_size) {
        smpi_mpi_recv(tmp_buf, count, dtype, src, tag, comm, &status);
        smpi_op_apply(op, tmp_buf, recv_buf, &count, &dtype);
        //printf("Node %d recv from node %d when mask is %d\n", rank, src, mask);
      }
    } else {

      dst = (inter_rank * num_core) + (intra_rank & (~mask));
      smpi_mpi_send(recv_buf, count, dtype, dst, tag, comm);
      //printf("Node %d send to node %d when mask is %d\n", rank, dst, mask);
      break;
    }
    mask <<= 1;
  }



  // INTER: reduce-scatter
  if (intra_rank == 0) {
    int send_offset, recv_offset;
    int seg_count = count / inter_comm_size;
    int to = ((inter_rank + 1) % inter_comm_size) * num_core;
    int from =
        ((inter_rank + inter_comm_size - 1) % inter_comm_size) * num_core;
    int i;

    //printf("node %d to %d from %d\n",rank,to,from);

    for (i = 0; i < (inter_comm_size - 1); i++) {

      send_offset =
          ((inter_rank - 1 - i +
            inter_comm_size) % inter_comm_size) * seg_count * extent;
      recv_offset =
          ((inter_rank - 2 - i +
            inter_comm_size) % inter_comm_size) * seg_count * extent;

      smpi_mpi_sendrecv((char *) recv_buf + send_offset, seg_count, dtype, to,
                   tag + i, tmp_buf, seg_count, dtype, from, tag + i, comm,
                   &status);

      // result is in rbuf
      smpi_op_apply(op, tmp_buf, (char *) recv_buf + recv_offset, &seg_count,
                     &dtype);
    }

    // INTER: allgather
    for (i = 0; i < (inter_comm_size - 1); i++) {

      send_offset =
          ((inter_rank - i +
            inter_comm_size) % inter_comm_size) * seg_count * extent;
      recv_offset =
          ((inter_rank - 1 - i +
            inter_comm_size) % inter_comm_size) * seg_count * extent;

      smpi_mpi_sendrecv((char *) recv_buf + send_offset, seg_count, dtype, to,
                   tag + i, (char *) recv_buf + recv_offset, seg_count, dtype,
                   from, tag + i, comm, &status);

    }
  }



     // INTER_binomial_reduce

     // only root node for each SMP
//     if (intra_rank == 0) {
//
//     mask = 1;
//     while (mask < inter_comm_size) {
//     if ((mask & inter_rank) == 0) {
//     src = (inter_rank | mask) * num_core;
//     if (src < comm_size) {
//     smpi_mpi_recv(tmp_buf, count, dtype, src, tag, comm, &status);
//     (* uop) (tmp_buf, recv_buf, &count, &dtype);
     //printf("Node %d recv from node %d when mask is %d\n", rank, src, mask);
//     }
//     }
//     else {
//     dst = (inter_rank & (~mask)) * num_core;
//     smpi_mpi_send(recv_buf, count, dtype, dst, tag, comm);
     //printf("Node %d send to node %d when mask is %d\n", rank, dst, mask);
//     break;
//     }
//     mask <<=1;
//     }
//     }

     // INTER_binomial_bcast


//     if (intra_rank == 0) {
//     mask = 1;
//     while (mask < inter_comm_size) {
//     if (inter_rank & mask) {
//     src = (inter_rank - mask) * num_core;
     //printf("Node %d recv from node %d when mask is %d\n", rank, src, mask);
//     smpi_mpi_recv(recv_buf, count, dtype, src, tag, comm, &status);
//     break;
//     }
//     mask <<= 1;
//     }
//
//     mask >>= 1;
     //printf("My rank = %d my mask = %d\n", rank,mask);

//     while (mask > 0) {
//     if (inter_rank < inter_comm_size) {
//     dst = (inter_rank + mask) * num_core;
//     if (dst < comm_size) {
     //printf("Node %d send to node %d when mask is %d\n", rank, dst, mask);
//     smpi_mpi_send(recv_buf, count, dtype, dst, tag, comm);
//     }
//     }
//     mask >>= 1;
//     }
//     }


  // INTRA_binomial_bcast

  int num_core_in_current_smp = num_core;
  if (inter_rank == (inter_comm_size - 1)) {
    num_core_in_current_smp = comm_size - (inter_rank * num_core);
  }
  //  printf("Node %d num_core = %d\n",rank, num_core_in_current_smp);
  mask = 1;
  while (mask < num_core_in_current_smp) {
    if (intra_rank & mask) {
      src = (inter_rank * num_core) + (intra_rank - mask);
      //printf("Node %d recv from node %d when mask is %d\n", rank, src, mask);
      smpi_mpi_recv(recv_buf, count, dtype, src, tag, comm, &status);
      break;
    }
    mask <<= 1;
  }

  mask >>= 1;
  //printf("My rank = %d my mask = %d\n", rank,mask);

  while (mask > 0) {
    dst = (inter_rank * num_core) + (intra_rank + mask);
    if (dst < comm_size) {
      //printf("Node %d send to node %d when mask is %d\n", rank, dst, mask);
      smpi_mpi_send(recv_buf, count, dtype, dst, tag, comm);
    }
    mask >>= 1;
  }

  smpi_free_tmp_buffer(tmp_buf);
  return MPI_SUCCESS;
}
