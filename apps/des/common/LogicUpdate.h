/** LogicUpdate corresponds to a change in the input or output of a gate -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2011, The University of Texas at Austin. All rights reserved.
 * UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES CONCERNING THIS
 * SOFTWARE AND DOCUMENTATION, INCLUDING ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR ANY PARTICULAR PURPOSE, NON-INFRINGEMENT AND WARRANTIES OF
 * PERFORMANCE, AND ANY WARRANTY THAT MIGHT OTHERWISE ARISE FROM COURSE OF
 * DEALING OR USAGE OF TRADE.  NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH
 * RESPECT TO THE USE OF THE SOFTWARE OR DOCUMENTATION. Under no circumstances
 * shall University be liable for incidental, special, indirect, direct or
 * consequential damages or loss of profits, interruption of business, or
 * related expenses which may arise from use of Software or Documentation,
 * including but not limited to those resulting from defects in Software and/or
 * Documentation, or loss or inaccuracy of data of any kind.
 *
 *  Created on: Jun 23, 2011
 *
 * @author M. Amber Hassaan <ahassaan@ices.utexas.edu>
 */

#ifndef LOGICUPDATE_H_
#define LOGICUPDATE_H_

#include <string>
#include <sstream>

#include "logicDefs.h"

/**
 * The Class LogicUpdate is the msg carried by events. represents a change in the value of a net.
 */
struct LogicUpdate {

  /** The net name. */
  std::string netName;

  /** The net val. */
  LogicVal netVal;

  /**
   * Instantiates a new logi update
   *
   * @param netName the net name
   * @param netVal the net val
   */
  LogicUpdate(std::string netName, LogicVal netVal) 
    : netName (netName), netVal (netVal) {}

  LogicUpdate (): netName (""), netVal('0') {}

  /**
   * string representation
   */
  const std::string toString() const {
    std::ostringstream ss;
    ss << "netName = " << netName << " netVal = " << netVal;
    return ss.str ();
  }

  /**
   * Gets the net name.
   *
   * @return the net name
   */
  const std::string getNetName() const {
    return netName;
  }

  /**
   * Sets the net name.
   *
   * @param netName the new net name
   */
  void setNetName(const std::string& netName) {
    this->netName = netName;
  }

  /**
   * Gets the net val.
   *
   * @return the net val
   */
  LogicVal getNetVal() const {
    return netVal;
  }

  /**
   * Sets the net val.
   *
   * @param netVal the new net val
   */
  void setNetVal(const LogicVal& netVal) {
    this->netVal = netVal;
  }
};
#endif /* LOGICUPDATE_H_ */
