import Vue from 'vue'
import Vuex from 'vuex'
import axios from 'axios'

Vue.use(Vuex)

export default new Vuex.Store({
  state: {
    chart_value: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
  },
  mutations: {
    update_chart_value (state, newvalue) {
      var weight = newvalue.trim().split(' ')
      for (let i = 0; i < weight.length; i+=2) {
        state.chart_value.push(parseInt(weight[i+1]))
        state.chart_value.shift()
        console.log(weight[i] + 'ms : ' + weight[i + 1])
      }
    }
  },
  actions: {
    update_chart_value ({ commit }) {
      axios.get('/api/v1/clima/data')
        .then(data => {
          commit('update_chart_value', data.data.weight)
        })
        .catch(error => {
          console.log(error)
        })
    }
  }
})
