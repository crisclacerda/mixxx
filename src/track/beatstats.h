#pragma once

#include <QQueue>
#include "util/assert.h"

/// These classes are used to compute statistics descriptors
/// of a series of tempo values and are called from beatutils

class WindowedStatistics {

    public:

        WindowedStatistics(int period) {
            DEBUG_ASSERT(period > 0);
            m_period = period;
        }
        virtual ~WindowedStatistics() = default;

        double operator()(double newValue) {
            double oldValue = updateWindow(newValue);
            update(newValue, oldValue);
            return compute();
        }
        double operator()(void) {
            return compute();
        }
        int lag() {
            // expected latency
            return (m_window.size() - 1)/2;
        }

    private:

        double updateWindow(double newValue) {
                m_window.enqueue(newValue);
            if (m_window.count() > m_period) {
                return m_window.dequeue();
            } else {
                return std::nan("");
            }
        }
        virtual double compute() = 0;
        virtual void update(double newValue, double oldValue) = 0;
        QQueue<double> m_window;
        int m_period;
};

class BeatStatistics {
  public:
    static double median(QList<double> sortedItems);
};

class MovingMedian : public WindowedStatistics {
    QList<double> m_sortedValues;
    void update(double, double) override;
    double compute() override;
    public:
      MovingMedian(int period)
              : WindowedStatistics(period) {};
      ~MovingMedian() = default;
};

class MovingMode : public WindowedStatistics {
    QMap<double, int> m_tempoFrequency;
    void update(double, double) override;
    double compute() override;
    public:
      MovingMode(int period)
              : WindowedStatistics(period) {};
      ~MovingMode() = default;
};
